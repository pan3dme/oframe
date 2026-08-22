/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

// 调试开关：在 include pan3dme.h 之前定义，覆盖默认值
// 1=开发模式（输出所有调试信息） 0=正式模式（仅输出关键信息）


#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <pan3dme.h>

// ==================== 全局变量 ====================
String deviceName;           // 设备名称
char sendData[BUFFER_SIZE];  // 发送数据缓存
// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）
bool timeSyncFlag = false;
bool isSleepRestFristSendRolaFlag = true;  //标记启动后的第一条LORA消息
int batteryNum = 100;
const uint64_t MAX_SLEEP_US = 4200000000ULL;

unsigned long gpsWorkTime = 0;
unsigned long gpsWorkInterval = 0;
unsigned long gpsWorkStat = 0;
int typeindex = FLAG_TYPE_0;


RTC_DATA_ATTR uint32_t rtcMagic;

RTC_DATA_ATTR long long lastSyncTime;



RTC_DATA_ATTR int loraTxPower;
RTC_DATA_ATTR int sendModeidx;

RTC_DATA_ATTR int16_t lastRssi;
RTC_DATA_ATTR int8_t lastSnr;



RTC_DATA_ATTR double rtc_gps_lat;
RTC_DATA_ATTR double rtc_gps_lon;

RTC_DATA_ATTR bool configConfirmed;
RTC_DATA_ATTR bool isNeedGpsWork;
RTC_DATA_ATTR int rtcSendCount;
RTC_DATA_ATTR int rtcResiveIdx;
RTC_DATA_ATTR int roundTime;

RTC_DATA_ATTR char needSendGpsStr[32];
RTC_DATA_ATTR char lastrelayName[10];
RTC_DATA_ATTR char work_time_str[16];
RTC_DATA_ATTR char gps_time_str[16];
RTC_DATA_ATTR char config_str[16];

#define MY_RTC_MAGIC 0xA5B6C7D8U


RadioEvents_t radioEvents;                             // LoRa事件回调
void printTimeToString(String str, unsigned long ms);  // 前向声明

// 根据work_time_str判断是否在工作时间，返回调整后的休眠微秒数
// work_time_str格式: "05:02-10:59" 表示工作时段 05:02 ~ 10:59
uint64_t getAdjustedSleepTimeUs(unsigned long sleepMs) {
  // 1. 用isTimeInRange判断是否在工作时间（支持跨午夜）
  bool inWorkTime = isTimeInRange(getCurrentTimestampSec(), work_time_str);
  if (inWorkTime || !isBoardDateTimeOK()) {
    DEBUG_PRINTLN("✅ 当前在工作时间内，按原计划休眠");
    return (uint64_t)sleepMs * 1000ULL;
  }

  // 2. 不在工作时间内，解析开始时间计算等待
  int startH = 0, startM = 0, endH = 0, endM = 0;
  int ret = sscanf(work_time_str, "%d:%d-%d:%d", &startH, &startM, &endH, &endM);
  if (ret != 4) {
    DEBUG_PRINTLN("⚠️ work_time_str parse fail, use 00:00‑23:59");
    return (uint64_t)sleepMs * 1000ULL;
  }
  int startMinutes = startH * 60 + startM;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t now = tv.tv_sec;
  struct tm t;
  localtime_r(&now, &t);
  int nowMinutes = t.tm_hour * 60 + t.tm_min;

  DEBUG_PRINTF("    工作时间窗口: %02d:%02d - %02d:%02d, 当前: %02d:%02d\n",
               startH, startM, endH, endM, t.tm_hour, t.tm_min);

  // 3. 计算到下次工作开始的分钟数
  int waitMinutes = 0;
  if (nowMinutes < startMinutes) {
    waitMinutes = startMinutes - nowMinutes;
  } else {
    waitMinutes = (24 * 60 - nowMinutes) + startMinutes;
  }

  uint64_t adjustedUs = (uint64_t)waitMinutes * 60 * 1000000ULL;
  DEBUG_PRINTF("❌ 当前不在工作时间，%d分钟后开始工作，休眠%llu秒\n",
               waitMinutes, adjustedUs / 1000000ULL);
  return adjustedUs;
}

unsigned long get_send_interval_ms() {
  if (roundTime == 0) {
    return SEND_INTERVAL_MS;
  } else {
    return roundTime;
  }
}
float getSlotDuration() {
  return get_send_interval_ms() / 30 / 1000;  // 30台设备现在是30分钟
}

// ==================== 计算下次发送时间 (修正版) ====================
unsigned long calculateNextTime(unsigned long intervalSeconds) {
  if (intervalSeconds == 0) {
    DEBUG_PRINTLN("⚠️ intervalSeconds为0，使用默认值600秒");
    intervalSeconds = 600;
  }
  if (deviceIndex < 0 || totalDevices == 0) {
    deviceIndex = getDevicesIdx();
    totalDevices = getTotalDevices();
    DEBUG_PRINTF("设备索引: %d, 总设备数: %d\n", deviceIndex, totalDevices);
  }
  if (deviceIndex < 0 || totalDevices <= 0) {
    DEBUG_PRINTLN("⚠️ 设备未认证，使用默认间隔");
    return millis() + intervalSeconds * 1000;
  }

  // 1. 获取当前时间
  String timeStr = getCurrentTime(true);
  int hour = 0, minute = 0, second = 0;
  sscanf(timeStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &hour, &minute, &second);
  unsigned long currentSeconds = hour * 3600 + minute * 60 + second;

  // 2. 计算基础参数
  unsigned long mySlotOffset =
    (unsigned long)((deviceIndex + 0.5) * getSlotDuration());  // 我在周期内的偏移量

  // 3. 核心修复逻辑：计算到下一个时隙的等待时间
  unsigned long cyclesPassed = currentSeconds / intervalSeconds;
  unsigned long lastTargetSeconds =
    cyclesPassed * intervalSeconds + mySlotOffset;

  long secondsDiff = 0;
  if (lastTargetSeconds < currentSeconds) {
    secondsDiff = intervalSeconds - (currentSeconds - lastTargetSeconds);
  } else {
    secondsDiff = lastTargetSeconds - currentSeconds;
  }
  if (secondsDiff < 0) {
    secondsDiff += intervalSeconds;
  }

  unsigned long delayMillis = secondsDiff * 1000;

  if (delayMillis == 0 && isSleepRestFristSendRolaFlag == true) {
    //这里是特殊处理如果属于休眠重启后如果是第一次获取发射时间并正好为0那么需要后置1秒这样才不会进入无效的重启。不然又会进入休眠
    delayMillis = 5;  //第一次获取就多出5秒做为容错
  }
  unsigned long minutes = delayMillis / 60000;
  unsigned long seconds = (delayMillis % 60000) / 1000;

  DEBUG_PRINTF("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu分%lu秒\n",
               timeStr.c_str(), deviceIndex, getSlotDuration(), minutes,
               seconds);



  return millis() + delayMillis;
}

// ==================== LoRa模块初始化 ====================
bool loraInitOk = false;
void initLora() {
  if (loraInitOk) {
    return;
  }
  loraInitOk = true;
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = OnRxDone;
  radioEvents.RxTimeout = OnRxTimeout;
  radioEvents.RxError = OnRxError;
  initPanRadio(&radioEvents, 22, 915000000, 11);
}
void OnRxTimeout(void) {
  DEBUG_PRINTLN("⚠️ Radio接收超时!");
  Radio.Sleep();
}

void OnRxError(void) {
  // 接收到错误，相当于有多个中继打架，暂做标记
  rtcResiveIdx = rtcSendCount;
  DEBUG_PRINTLN("❌ Radio接收错误!");
  Radio.Sleep();
}
String extractDeviceIdFromInfo(String infoStr) {
  int first = infoStr.indexOf('|');
  if (first == -1) {
    return "";
  }
  int second = infoStr.indexOf('|', first + 1);
  if (second == -1) {
    return infoStr.substring(first + 1);
  }
  return infoStr.substring(first + 1, second);
}
bool isMyDeviceInList(String infoStr, String targetDevice) {
  String id = extractDeviceIdFromInfo(infoStr);
  return id.equals(targetDevice);
}
void meshSynTime(String infoStr, int firstPipeIndex) {
  int secondPipeIndex = infoStr.indexOf('|', firstPipeIndex + 1);
  if (secondPipeIndex < 0) {
    DEBUG_PRINTLN("❌ SYN_TIME格式错误：缺少第二个分隔符");
    return;
  }

  // 第一个| ~第二个|：时间戳
  String timeStr = infoStr.substring(firstPipeIndex + 1, secondPipeIndex);
  // 第二个|后面全部：relayName，trim过滤\r\n空格
  String relayName = infoStr.substring(secondPipeIndex + 1);
  relayName.trim();

  DEBUG_PRINT("本机时间");
  DEBUG_PRINTLN(getCurrentTime(true));

  // 第三个|后面全部就是 relayName

  if (relayName.length() > 0) {
    DEBUG_PRINT("中继名: ");
    DEBUG_PRINTLN(relayName);
    if (lastSyncTime > 0 && strcmp(lastrelayName, relayName.c_str()) == 0) {
      DEBUG_PRINTLN("---中继和上次相对，那开始计算晶震偏移:----- ");
      long long ds =
        getCurrentTimestampSec() - lastSyncTime;

      printDurationSec(ds, "两次对时间隔: ");
      long long diff_Sec = mathTimeDiffmsFromSec(atoll(timeStr.c_str()));
      printDurationSec(diff_Sec, "当前偏差: ");
      // 计算每小时偏差 = 偏差 * 1小时 / 本机经过时间
      if (ds > 0) {
        long long hourlyDriftSec = diff_Sec * 3600LL / ds;
        DEBUG_PRINT("每小时偏差: ");
        DEBUG_PRINT(hourlyDriftSec);
        DEBUG_PRINTLN(" 秒");
        printDurationSec(hourlyDriftSec, "每小时偏差: ");
        // 每小时小于3分钟的偏差才通过，防止出乱子
        if (sendModeidx > 0) {
          sendLoraToMid(String(MSG_TYPE_WARN) + "|" + deviceName + "|hourTm|" + hourlyDriftSec, true);
          delay(2000);
        }
      }
    }

    strncpy(lastrelayName, relayName.c_str(), sizeof(lastrelayName) - 1);
    lastrelayName[sizeof(lastrelayName) - 1] = '\0';
  }

  setTimeFromTimestampSec(atoll(timeStr.c_str()));
  lastSyncTime = getCurrentTimestampSec();
  timeSyncFlag = true;
}
void meshCmdType(String infoStr, String tmp) {
  int pos0 = infoStr.indexOf('|');
  int pos1 = infoStr.indexOf('|', pos0 + 1);
  int pos2 = infoStr.indexOf('|', pos1 + 1);
  int pos3 = infoStr.indexOf('|', pos2 + 1);

  String thirdField;
  // 下标2字段：pos1+1 到 pos2
  if (pos2 != -1) {
    thirdField = infoStr.substring(pos1 + 1, pos2);
  } else {
    thirdField = "";
  }
  DEBUG_PRINT(" thirdField=");
  DEBUG_PRINTLN(thirdField);
  if (thirdField == "A") {
    //10,1m,38
    int rt;
    char workstr[16];
    char gpsstr[16];
    if (sscanf(tmp.c_str(), "%d,%15[^,],%15s", &rt, workstr, gpsstr) == 3) {
      if (rt < 5 || rt > 120) {
        DEBUG_PRINT("❌上报周期最小5分钟最大不超过2小时 ");
        return;
      }
      roundTime = rt * 60 * 1000;



      tmp.toCharArray(config_str, sizeof(config_str) - 1);
      config_str[sizeof(config_str) - 1] = '\0';
      configConfirmed = true;
      DEBUG_PRINT("全局配置");
      uint8_t outS, outE;
      if (indexToTimeWindow(twoCharToIndex(workstr), outS, outE)) {
        uint8_t endHour = outE;
        uint8_t endMin = 0;
        if (outE == 23) {
          endMin = 59;
        }


        snprintf(work_time_str, sizeof(work_time_str), "%02d:00-%02d:%02d", outS, endHour, endMin);

        DEBUG_PRINT(" work=");
        DEBUG_PRINT(work_time_str);
      }
      if (indexToTimeWindow(twoCharToIndex(gpsstr), outS, outE)) {
        uint8_t endHour = outE;
        uint8_t endMin = 0;
        if (outE == 23) {
          endMin = 59;
        }
        snprintf(gps_time_str, sizeof(gps_time_str), "%02d:00-%02d:%02d", outS, endHour, endMin);
        DEBUG_PRINT(" gps=");
        DEBUG_PRINT(gps_time_str);
      }
    } else {
      DEBUG_PRINT("❌配置格式错误 ");
    }
  } else if (thirdField == "minbattery") {
    int modeVal = tmp.toInt();

    DEBUG_PRINT("✅✅设置最底工作电量：");

  } else if (thirdField == "sendmode") {
    // 11|v4-10|sendmode|1|0
    int modeVal = tmp.toInt();

    sendModeidx = modeVal;
    DEBUG_PRINT("✅✅修改上报模式：");

  } else if (thirdField == "txpower") {
    DEBUG_PRINT("✅✅修改发射功率：");
    if (tmp.toInt() >= 10 && tmp.toInt() <= 28) {
      loraTxPower = tmp.toInt();
    }
  } else if (thirdField == "upgps") {

    DEBUG_PRINT("✅✅时时定位改成只跟踪1分钟，正好利用现有机制");
    typeindex = FLAG_TYPE_3;
    gpsWorkStat = millis() + 5000;    // 延时5秒
    gpsWorkTime = 1 * 60 * 1000;      // 跟踪时间
    gpsWorkInterval = 1 * 60 * 1000;  // 跟踪上报间隔

  } else if (thirdField == "B") {
    // 11|v4-10|follow|30,5
    int commaIndex = tmp.indexOf(',');
    if (commaIndex != -1) {
      String firstStr = tmp.substring(0, commaIndex);
      String secondStr = tmp.substring(commaIndex + 1);
      int firstNum = firstStr.toInt();
      int secondNum = secondStr.toInt();
      DEBUG_PRINTLN(firstNum);   // 输出 30
      DEBUG_PRINTLN(secondNum);  // 输出 5
      if (firstNum > 5 && firstNum < 60 && secondNum > 0 && secondNum <= firstNum) {
        DEBUG_PRINT("✅✅上报GPS坐标：");
        typeindex = FLAG_TYPE_3;
        gpsWorkStat = millis() + 5000;            // 延时5秒
        gpsWorkTime = firstNum * 60 * 1000;       // 跟踪时间
        gpsWorkInterval = secondNum * 60 * 1000;  // 跟踪上报间隔
      }

    } else {
      // 没有逗号的处理逻辑
      DEBUG_PRINTLN("没有找到逗号");
    }

  } else {
    DEBUG_PRINTLN("❌❌❌❌ 需要补充功能列表");
    return;
  }
}
// LoRa接收回调
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size >= BUFFER_SIZE) {
    return;
  }
  Radio.Sleep();
  lastRssi = rssi;
  lastSnr = snr;
  char buf[BUFFER_SIZE];
  memcpy(buf, payload, size);
  buf[size] = '\0';
  DEBUG_PRINTLN("");
  DEBUG_PRINT("接收：");
  DEBUG_PRINTLN(buf);

  DEBUG_PRINT(" rssi：");
  DEBUG_PRINT(lastRssi);
  DEBUG_PRINT(" snr");
  DEBUG_PRINTLN(lastSnr);


  // 检查是否为GPS高频指令（5|v4-6,...）且目标是当前设备
  String infoStr(buf);
  int firstPipeIndex = infoStr.indexOf('|');
  if (firstPipeIndex <= 0) {
    return;
  }
  // 标记接收窗口信息 记录下IDX然后用于判断是否握手成功
  rtcResiveIdx = rtcSendCount;
  int messageType = infoStr.substring(0, firstPipeIndex).toInt();
  int lastPipe = infoStr.lastIndexOf('|');
  String tmp = infoStr.substring(lastPipe + 1);  // 从最后一个'|'后取到末尾

  if (messageType == MSG_TYPE_COM && isMyDeviceInList(infoStr, deviceName)) {
    meshCmdType(infoStr, tmp);
  }

  if (messageType == MSG_TYPE_SYN_TIME) {
    meshSynTime(infoStr, firstPipeIndex);
  }
}
//发送LORA到中继
void sendLoraToMid(String dataStr, bool addBatter) {
  if (loraInitOk == false) {
    initLora();
    delay(100);
  }

  if (addBatter == true) {
    dataStr += "|" + String(batteryNum);
  }
  dataStr += "|" + String(rtcSendCount++);
  sendData[0] = 0;
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    DEBUG_PRINTLN("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }
  DEBUG_PRINT("发送：");
  DEBUG_PRINT(sendData);
  DEBUG_PRINT("  len:");
  DEBUG_PRINTLN(strlen(sendData));
  Radio.Send((uint8_t *)sendData, strlen(sendData));

  isSleepRestFristSendRolaFlag = false;
}
// ==================== 构建并发送数据包 ====================

// 结果接收窗口
unsigned long inRxEndTime = 0;
// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  DEBUG_PRINT("✅ 发送完成");
  if (typeindex == FLAG_TYPE_1) {
    DEBUG_PRINTLN("1 定时 上报信息");
    inRxEndTime = millis() + 3000;  // 4秒后结束接收窗口
    typeindex = FLAG_TYPE_2;
    Radio.Rx(0);
  } else if (typeindex == FLAG_TYPE_2) {
    DEBUG_PRINTLN("2 应该不会到这里");
  } else if (typeindex == FLAG_TYPE_3) {
    DEBUG_PRINTLN("3 Gps上报完成");
  }
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {

  DEBUG_PRINTLN("❌ 发送超时");
  typeindex = FLAG_TYPE_0;
}

void printTimeToString(String str, unsigned long ms) {
  int totalSec = ms / 1000;
  int min = totalSec / 60;
  int sec = totalSec % 60;
  int remMs = ms % 1000;
  DEBUG_PRINT(str);
  DEBUG_PRINT(min);
  DEBUG_PRINT("分");
  DEBUG_PRINT(sec);
  DEBUG_PRINT("秒");
  DEBUG_PRINT(remMs);
  DEBUG_PRINTLN("毫秒");
}
int seacthTm = 240000;  //5分钟提前搜星
void meshGpsInfoFun(bool closeGps = true) {
  if (!getGpsStatus()) {
    initPanGPS();
  }
  unsigned long startAttemptTime = millis();
  int skipnum = 0;
  while (true) {
    gpsEncode();
    bool hasLocValid = gps.location.isValid();
    bool yearOk = (gps.date.year() > 2025);
    bool gpsReliable = isReliableGPS();
    bool ellitesNum = gps.satellites.value() >= 6;

    bool timeoutOk = (millis() - startAttemptTime) < seacthTm;
    // Serial.print(".");
    // Serial.println(getCurrentTime());
    showDisplayBy4Area(deviceName, getGpsInfoStr(), getCurrentTime(false),
                       String(skipnum++));
    DEBUG_PRINT(hasLocValid ? "✅" : "❌");
    DEBUG_PRINT("定位数据:");
    if (hasLocValid) {
      DEBUG_PRINT(getGpsInfoStr());
    }
    DEBUG_PRINT(yearOk ? "✅" : "❌");
    DEBUG_PRINT(" 年份>2025:");

    DEBUG_PRINT(gpsReliable ? "✅" : "❌");
    DEBUG_PRINT(" GPS可靠:");

    DEBUG_PRINT(ellitesNum ? "✅" : "❌");
    DEBUG_PRINT(" 卫星数量:");
    DEBUG_PRINT(gps.satellites.value());
    DEBUG_PRINT("   ");

    DEBUG_PRINT(timeoutOk ? "" : "❌");
    DEBUG_PRINT("搜星时间:(");
    int sec = (millis() - startAttemptTime) / 1000;
    DEBUG_PRINT(sec);
    DEBUG_PRINT("秒)    ");
    DEBUG_PRINTLN(getCurrentTime(true));



    bool allPass = (hasLocValid && yearOk && gpsReliable) && timeoutOk;
    if (allPass) {
      DEBUG_PRINTLN("==== GPS全部条件满足，退出搜星循环 ====");
      upDataGpsTimeToCs();

      lastSyncTime = getCurrentTimestampSec();  // ← 加上
      strncpy(lastrelayName, "", sizeof(lastrelayName) - 1);
      lastrelayName[sizeof(lastrelayName) - 1] = '\0';
      timeSyncFlag = true;

      break;
    }
    if (!timeoutOk) {
      DEBUG_PRINT("==== 搜星 ");
      DEBUG_PRINT(seacthTm / 1000);
      DEBUG_PRINTLN(" 秒超时，强制退出 ====");
      break;
    }
    delay(1000);
  }
  DEBUG_PRINTLN(getGpsInfoStr());
  if (closeGps) {
    setGpsEnable(false);
  }
}
String lastPrintTimeStr = "";  // 存储上次打印的时间字符串（不含毫秒）
void printCurrentTime() {
  String nowStr = getCurrentTime(true);
  int dotIndex = nowStr.indexOf('.');
  String nowSecStr =
    (dotIndex != -1) ? nowStr.substring(0, dotIndex) : nowStr;  // 去掉毫秒
  if (lastPrintTimeStr != nowSecStr) {
    DEBUG_PRINTLN(nowStr);  // 或者只打印秒级字符串
    lastPrintTimeStr = nowSecStr;
  }
}

void batteryLowSleep(int minValue) {
  if (batteryNum < 0 || batteryNum > 100) return;
  if (batteryNum <= minValue) {
    DEBUG_PRINTLN("❌❌❌电压过底电压过底电压过底❌❌❌");
    DEBUG_PRINTLN("电压过底，休眠24个小时");
    delay(1000);
    DEBUG_PRINT("❌");

    esp_sleep_enable_timer_wakeup(MAX_SLEEP_US);
    DEBUG_PRINTLN("--->即将进入深度睡眠...");
    Serial.flush();
    Radio.Sleep();

    esp_deep_sleep_start();
  }
}

unsigned long num6000 = 5000;  // 暂时提前20秒开机
void testSheepFun(bool driftComp) {

  unsigned long waittm = (nextSendTime >= millis()) ? (nextSendTime - millis()) : 0;

  printTimeToString("到上报时间还有 ", nextSendTime - millis());
  // 测试阶段多给一点时间用于烧入程序  num6000 = 10000;
  if ((waittm) > num6000) {
    if (configConfirmed || rtcSendCount <= 1) {
      //确认配置给两秒特殊插入数据， 开机上报当前的配置信息
      configConfirmed = false;
      sendLoraToMid(String(MSG_TYPE_CONFIG) + "|" + deviceName + "|" + String(config_str), false);
      delay(2000);
      waittm = (nextSendTime >= millis()) ? (nextSendTime - millis()) : 0;
    }


    DEBUG_PRINT("距离上报时间超过 ");
    DEBUG_PRINT(num6000 / 1000);
    DEBUG_PRINTLN("秒进入睡眠");
    hideOLED();
    delay(1000);
    // 05:02|10:59
    uint64_t sleepTime = getAdjustedSleepTimeUs(waittm - num6000);
    //判断下个时间段是否需要开启GPS是的话就提前搜星
    if (isTimeInRange(getCurrentTimestampSec(), gps_time_str) && strlen(needSendGpsStr) == 0) {
      DEBUG_PRINTLN("工作模式上报GPS，需要提前开启GPS");
      if (sleepTime > (seacthTm * 1000ULL)) {
        sleepTime = sleepTime - (seacthTm * 1000ULL);
      } else {
        //小于时间周期，10秒就马上重启
        sleepTime = 10 * 1000 * 1000ULL;
      }
    }


    //判断是否需要GPS工作
    isNeedGpsWork = isTimeInRange(getCurrentTimestampSec(), gps_time_str);

    // if (sleepTime > MAX_SLEEP_US) {
    //   sleepTime = MAX_SLEEP_US;
    // }
    esp_sleep_enable_timer_wakeup(sleepTime);
    DEBUG_PRINTLN("--->即将进入深度睡眠...");
    // 计算并打印预计开机时间
    {
      uint64_t wakeUpSec = sleepTime / 1000000ULL;
      time_t now = time(nullptr);
      time_t wakeTime = now + (time_t)wakeUpSec;
      struct tm wt;
      localtime_r(&wakeTime, &wt);
      DEBUG_PRINTF("预计开机时间: %02d:%02d:%02d (休眠%llu秒)\n",
                   wt.tm_hour, wt.tm_min, wt.tm_sec, wakeUpSec);
    }
    Serial.flush();
    Radio.Sleep();
    esp_deep_sleep_start();
  }
}
String mathGpsRectByBaseStr(char *value) {
  char gpsOutBuf[32];
  filterGpsByRect(value, gpsOutBuf, rtc_gps_lat, rtc_gps_lon, 0.180, 0.202);
  return String(gpsOutBuf);
}
// ==================== 系统初始化 ====================
void setup() {
  Serial.begin(115200);

  if (rtcMagic != MY_RTC_MAGIC) {
    // ========== 全部出厂默认值写在这里 ==========
    lastSyncTime = 0;
    sendModeidx = 0;

    loraTxPower = 22;
    lastRssi = 0;
    lastSnr = 0;


    rtc_gps_lat = static_gps_lat;
    rtc_gps_lon = static_gps_lon;

    configConfirmed = false;
    isNeedGpsWork = false;
    rtcSendCount = -1;
    rtcResiveIdx = 0;
    roundTime = 0;

    strncpy(needSendGpsStr, "", sizeof(needSendGpsStr) - 1);
    needSendGpsStr[sizeof(needSendGpsStr) - 1] = '\0';

    strncpy(lastrelayName, "", sizeof(lastrelayName) - 1);
    lastrelayName[sizeof(lastrelayName) - 1] = '\0';

    strncpy(work_time_str, "00:00-23:59", sizeof(work_time_str) - 1);
    work_time_str[sizeof(work_time_str) - 1] = '\0';

    strncpy(gps_time_str, "09:00-13:00", sizeof(gps_time_str) - 1);
    gps_time_str[sizeof(gps_time_str) - 1] = '\0';

    strncpy(config_str, "5,0M,2o", sizeof(config_str) - 1);
    config_str[sizeof(config_str) - 1] = '\0';



    rtcMagic = MY_RTC_MAGIC;
    DEBUG_PRINTLN("INFO: RTC magic invalid -> reset all rtc params");
  }


  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  deviceName = makeDivceName();
  DEBUG_PRINT("开机时间-");
  DEBUG_PRINTLN(getCurrentTime(true));
  DEBUG_PRINT("开机时间-");
  DEBUG_PRINTLN(getCurrentTime(true));
  batteryNum = readBatteryEndStr();





  if (rtcSendCount == -1) {
    //重新启动不要快速进入休眠，是为了给出时间上传程序 烧入程序等待20秒才可以把程序上传不然很麻烦只为烧入程序
    unsigned long endTm = millis() + 20000;
    while (endTm > millis()) {
      delay(1000);
      DEBUG_PRINT("x");
    }
    rtcSendCount = 0;
  }
  batteryLowSleep(30);
  if (isNeedGpsWork && strlen(needSendGpsStr) == 0) {
    meshGpsInfoFun(true);
    strncpy(needSendGpsStr, getGpsInfoStr().c_str(), sizeof(needSendGpsStr) - 1);
    needSendGpsStr[sizeof(needSendGpsStr) - 1] = '\0';
  }
  if (rtcSendCount <= 0) {
    //直接前往上报
    nextSendTime = millis();
  } else {
    //获取设备的上报时间
    nextSendTime = calculateNextTime(get_send_interval_ms() / 1000);
    testSheepFun(false);
  }
  initLora();
}
// ==================== 主循环 ====================
void loop() {
  Radio.IrqProcess();
  if (typeindex == FLAG_TYPE_3) {
    Radio.Sleep();  // IrqProcess已完成，安全Sleep
    if (millis() < gpsWorkStat) {
      DEBUG_PRINTLN("准备跟踪GPS位置");
      delay(1000);  // 上报LORA需要2秒钟间隔
      return;
    }
    unsigned long gpsFollowStartm = millis();
    DEBUG_PRINT("gps持续准备上报位置:");
    DEBUG_PRINT(millis() - gpsWorkStat);
    DEBUG_PRINT("-");
    DEBUG_PRINTLN(gpsWorkTime);
    showOLED();
    DEBUG_PRINT("获取GPS ");
    printCurrentTime();
    meshGpsInfoFun(true);


    char gpsStr[32];

    strncpy(gpsStr, getGpsInfoStr().c_str(), sizeof(gpsStr) - 1);
    gpsStr[sizeof(gpsStr) - 1] = '\0';

    sendLoraToMid(String(MSG_TYPE_UP_GPS) + "|" + deviceName + "|" + mathGpsRectByBaseStr(gpsStr), false);
    delay(2000);  // 上报LORA需要2秒钟间隔
    hideOLED();

    if ((millis() - gpsWorkStat) > gpsWorkTime || gpsWorkInterval == gpsWorkTime) {
      DEBUG_PRINTLN("结束GPS上报");
      nextSendTime = 0;
      typeindex = FLAG_TYPE_0;
    } else {
      printTimeToString("延时 ", gpsWorkInterval);
      printTimeToString("还会持续", gpsWorkTime - (millis() - gpsWorkStat));
      if (gpsWorkInterval > (millis() - gpsFollowStartm)) {
        delay(gpsWorkInterval - (millis() - gpsFollowStartm));  // 延时设定时间
      }
    }
    return;
  } else if (typeindex == FLAG_TYPE_2) {
    delay(100);
    DEBUG_PRINT(".");
    if (inRxEndTime < millis()) {
      DEBUG_PRINT(rtcResiveIdx);
      DEBUG_PRINT("-");
      DEBUG_PRINT(rtcSendCount);
      // 回到普通模式，准备可以休眠
      nextSendTime = 0;
      typeindex = FLAG_TYPE_0;
      DEBUG_PRINTLN("");
      Radio.Sleep();
    }
    return;
  } else if (typeindex == FLAG_TYPE_1) {
    delay(10);
    return;
  } else if (typeindex == FLAG_TYPE_0) {



    if (isSleepRestFristSendRolaFlag == true) {  //一次重启只会上报一条LORA消息，
      if (nextSendTime < millis() || rtcSendCount == 0) {
        typeindex = FLAG_TYPE_1;
        if (strlen(needSendGpsStr) > 0) {
          sendLoraToMid(String(MSG_TYPE_GPS) + "|" + deviceName + "|" + mathGpsRectByBaseStr(needSendGpsStr), false);
          strncpy(needSendGpsStr, "", sizeof(needSendGpsStr) - 1);
          needSendGpsStr[sizeof(needSendGpsStr) - 1] = '\0';
        } else {
          sendLoraToMid(String(MSG_TYPE_TIME) + "|" + deviceName + "|" + getTodaySecond(), true);
        }
      }
    } else {
      if (nextSendTime < millis()) {
        nextSendTime = calculateNextTime(get_send_interval_ms() / 1000);
        if (timeSyncFlag) {  // 接收了同步时间

          if ((nextSendTime - millis()) < get_send_interval_ms() * 0.5) {
            DEBUG_PRINTLN("⚠️⚠️⚠️接收了同步时间，由于时间偏差导致又进入了上报窗口所以"
                          "要跳过这个窗口将时间后移到下一个周期⚠️⚠️⚠️");
            nextSendTime = nextSendTime + get_send_interval_ms();
          }
          timeSyncFlag = false;
        }
        testSheepFun(true);
      }
    }
  }
  printCurrentTime();
  delay(10);
}