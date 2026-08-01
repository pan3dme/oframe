/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

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

bool timeSynFlage = false;

String batterystr = "";

unsigned long gpsWorkTime = 0;
unsigned long gpsWorkInterval = 0;
unsigned long gpsWorkStat = 0;
int typeindex = FLAG_TYPE_0;


RTC_DATA_ATTR long long lastSendTimeTemp = 0;   //
RTC_DATA_ATTR long long lastDriftCompMs = 0;    //
RTC_DATA_ATTR long long hourlyDriftMsTemp = 0;  // 每个小时的时间偏差
RTC_DATA_ATTR int loraTxPower = 0;
RTC_DATA_ATTR int sendmodeFlage = 0;  //工作模式  0默认 1需要验证上传成功1次
RTC_DATA_ATTR int rtcSendCount = -1;
RTC_DATA_ATTR int rtcResiveIdx = 0;
RTC_DATA_ATTR int roundTime = 0;                     // 默认上报周末使用系统配置
RTC_DATA_ATTR char needSendGpsStr[32] = "";          //
RTC_DATA_ATTR char lastrelayName[32] = "";           //
RTC_DATA_ATTR char work_time_str[32] = "0:0-24:59";  // 默认工作时间

RadioEvents_t radioEvents;                             // LoRa事件回调
void printTimeToString(String str, unsigned long ms);  // 前向声明

// 根据work_time_str判断是否在工作时间，返回调整后的休眠微秒数
// work_time_str格式: "05:02-10:59" 表示工作时段 05:02 ~ 10:59
uint64_t getAdjustedSleepTimeUs(unsigned long sleepMs) {
  // 1. 解析工作时间窗口
  int startH = 0, startM = 0, endH = 0, endM = 0;
  sscanf(work_time_str, "%d:%d-%d:%d", &startH, &startM, &endH, &endM);
  int startMinutes = startH * 60 + startM;
  int endMinutes = endH * 60 + endM;

  // 2. 获取当前时间
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t now = tv.tv_sec;
  struct tm t;
  localtime_r(&now, &t);
  int nowMinutes = t.tm_hour * 60 + t.tm_min;

  Serial.printf("工作时间窗口: %02d:%02d - %02d:%02d, 当前: %02d:%02d\n",
                startH, startM, endH, endM, t.tm_hour, t.tm_min);

  // 3. 判断是否在工作时间内
  bool inWorkTime = (nowMinutes >= startMinutes && nowMinutes <= endMinutes);
  if (inWorkTime || !haveRightTime()) {
    Serial.println("✅ 当前在工作时间内，按原计划休眠");
    return (uint64_t)sleepMs * 1000ULL;
  }

  // 4. 不在工作时间内，计算到下次工作开始的秒数
  int waitMinutes = 0;
  if (nowMinutes < startMinutes) {
    // 当前在工作时间之前，等到今天的工作开始
    waitMinutes = startMinutes - nowMinutes;
  } else {
    // 当前已过工作时间，等到明天的工作开始
    waitMinutes = (24 * 60 - nowMinutes) + startMinutes;
  }

  uint64_t adjustedUs = (uint64_t)waitMinutes * 60 * 1000000ULL;
  Serial.printf("❌ 当前不在工作时间，%d分钟后开始工作，休眠%llu秒\n",
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
unsigned long calculateNextSendTime(unsigned long intervalSeconds) {
  if (deviceIndex < 0 || totalDevices == 0) {
    deviceIndex = getDevicesIdx();
    totalDevices = getTotalDevices();
    Serial.printf("设备索引: %d, 总设备数: %d\n", deviceIndex, totalDevices);
  }
  if (deviceIndex < 0 || totalDevices <= 0) {
    Serial.println("⚠️ 设备未认证，使用默认间隔");
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
  unsigned long minutes = delayMillis / 60000;
  unsigned long seconds = (delayMillis % 60000) / 1000;

  Serial.printf("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu分%lu秒\n",
                timeStr.c_str(), deviceIndex, getSlotDuration(), minutes,
                seconds);

  return millis() + delayMillis;
}

// ==================== LoRa模块初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = OnRxDone;
  radioEvents.RxTimeout = OnRxTimeout;
  radioEvents.RxError = OnRxError;
  bool isV4 = deviceName.startsWith("v4-");
  if (loraTxPower == 0) {
    initPanRadio(&radioEvents, TX_POWER);
  } else {
    initPanRadio(&radioEvents, loraTxPower);
  }
}
void OnRxTimeout(void) {
  Serial.println("⚠️ Radio接收超时!");
  Radio.Rx(0);
}

void OnRxError(void) {
  // 接收到错误，相当于有多个中继打架，暂做标记
  rtcResiveIdx = rtcSendCount;
  Serial.println("❌ Radio接收错误!");
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
// LoRa接收回调
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size >= BUFFER_SIZE) {
    return;
  }

  char buf[BUFFER_SIZE];
  memcpy(buf, payload, size);
  buf[size] = '\0';
  Serial.println("");
  Serial.print("接收到中继下发ROLA ：");
  Serial.println(buf);

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
    if (infoStr.indexOf("worktime") != -1) {
      // 11|v4-10|work_time|05:02-10:59
      Serial.print("✅✅设置工作时间：");
      tmp.toCharArray(work_time_str, sizeof(work_time_str));
      Serial.println(work_time_str);
    } else if (infoStr.indexOf("sendmode") != -1) {
      // 11|v4-10|sendmode|1|0
      Serial.print("✅✅设置工作模式：");
      sendmodeFlage = tmp.toInt();
      Serial.println(sendmodeFlage);
    } else if (infoStr.indexOf("setinterval") != -1) {
      // 11|v4-10|setinterval|30
      Serial.print("✅✅设置上报周期：");
      roundTime = tmp.toInt() * 60 * 1000;
      Serial.print("修改上报时间周末roundTime");
      Serial.println(roundTime);
    } else if (infoStr.indexOf("txpower") != -1) {
      Serial.print("✅✅修改发射功率：");
      loraTxPower = tmp.toInt();
    } else if (infoStr.indexOf("follow") != -1) {
      // 11|v4-10|follow|30,5
      int commaIndex = tmp.indexOf(',');
      if (commaIndex != -1) {
        String firstStr = tmp.substring(0, commaIndex);
        String secondStr = tmp.substring(commaIndex + 1);
        int firstNum = firstStr.toInt();
        int secondNum = secondStr.toInt();
        Serial.println(firstNum);   // 输出 30
        Serial.println(secondNum);  // 输出 5
        if (firstNum > 0 && firstNum < 60 && secondNum > 0 && secondNum < 60) {
          Serial.print("✅✅上报GPS坐标：");
          typeindex = FLAG_TYPE_3;
          gpsWorkStat = millis() + 5000;            //延时5秒
          gpsWorkTime = firstNum * 60 * 1000;       //跟踪时间
          gpsWorkInterval = secondNum * 60 * 1000;  //跟踪上报间隔
        }

      } else {
        // 没有逗号的处理逻辑
        Serial.println("没有找到逗号");
      }


    } else {
      Serial.println("❌❌❌❌ 需要补充功能列表");
      return;
    }
  }

  if (messageType == MSG_TYPE_SYN_TIME) {

    int secondPipeIndex = infoStr.indexOf('|', firstPipeIndex + 1);
    if (secondPipeIndex < 0) {
      Serial.println("❌ SYN_TIME格式错误：缺少第二个分隔符");
      return;
    }
    String timeStr = infoStr.substring(secondPipeIndex + 1);
    int timePipeIdx = timeStr.indexOf('|');
    if (timePipeIdx < 0) {
      Serial.println("❌ SYN_TIME格式错误：时间字段后缺少分隔符");
      return;
    }
    timeStr = timeStr.substring(0, timePipeIdx);
    Serial.print("本机时间");
    Serial.println(getCurrentTime(true));

    int lastPipeIdx = infoStr.lastIndexOf('|');
    String relayName = infoStr.substring(lastPipeIdx + 1);
    if (relayName.length() > 0) {
      Serial.print("中继名: ");
      Serial.println(relayName);
      if (lastSendTimeTemp > 0 && strcmp(lastrelayName, relayName.c_str()) == 0) {
        Serial.println("---中继和上次相对，那开始计算晶震偏移:----- ");
        long long ds = getCurrentTimestampMs() + lastDriftCompMs - lastSendTimeTemp;

        printDurationMs(ds, "本机周期: ");
        long long diff_ms = mathTimeDiffmstimeFromLora(timeStr) + lastDriftCompMs;
        printDurationMs(diff_ms, "当前偏差: ");
        // 计算每小时偏差 = 偏差 * 1小时 / 本机经过时间
        if (ds > 0) {
          long long hourlyDriftMs = diff_ms * 3600000LL / ds;
          Serial.print("每小时偏差: ");
          Serial.print(hourlyDriftMs);
          Serial.println(" 毫秒");
          printDurationMs(hourlyDriftMs, "每小时偏差: ");
          //每小时小于31秒的偏差才通过，防止出乱子
          if (abs(hourlyDriftMs) < 31000) {
            hourlyDriftMsTemp = hourlyDriftMs;
          }
        }
      }
      strcpy(lastrelayName, relayName.c_str());
    }

    setTimeFromLora(timeStr);
    lastSendTimeTemp = getCurrentTimestampMs();
    timeSynFlage = true;
  }
}
void sendLoraToMid(String dataStr) {

  dataStr += "|" + batterystr;
  dataStr += "|" + String(rtcSendCount++);
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }

  Serial.print("发送：");
  Serial.print(sendData);
  Serial.print("  len:");
  Serial.println(strlen(sendData));

  Radio.Send((uint8_t *)sendData, strlen(sendData));
  delay(100);
}
// ==================== 构建并发送数据包 ====================

// 结果接收窗口
unsigned long inRxEndTime = 0;
// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  // Radio.Sleep();
  Serial.print("✅ 发送完成");
  if (typeindex == FLAG_TYPE_1) {
    Serial.println("定时上报信息");
    inRxEndTime = millis() + 5000;  // 5秒后结束接收窗口
    typeindex = FLAG_TYPE_2;
    Radio.Rx(0);
  } else if (typeindex == FLAG_TYPE_2) {
    Serial.println("重复上报时间信息");
  } else if (typeindex == FLAG_TYPE_3) {
    Serial.println("Gps上报完成");
  }
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {
  // Radio.Sleep();  // 中断中不应操作Radio硬件，移到loop处理
  Serial.println("❌ 发送超时");
  typeindex = FLAG_TYPE_0;
}

void printTimeToString(String str, unsigned long ms) {
  int totalSec = ms / 1000;
  int min = totalSec / 60;
  int sec = totalSec % 60;
  int remMs = ms % 1000;
  Serial.print(str);
  Serial.print(min);
  Serial.print("分");
  Serial.print(sec);
  Serial.print("秒");
  Serial.print(remMs);
  Serial.println("毫秒");
}
int seacthTm = 180000;
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
    bool timeoutOk = (millis() - startAttemptTime < seacthTm);
    // Serial.print(".");
    // Serial.println(getCurrentTime());
    showDisplayBy4Area(deviceName, getGpsInfoStr(), getCurrentTime(false),
                       String(skipnum++));
    Serial.print(hasLocValid ? "✅" : "❌");
    Serial.print("定位有效:");
    if (hasLocValid) {
      Serial.print(getGpsInfoStr());
    }
    Serial.print(yearOk ? "✅" : "❌");
    Serial.print(" 年份>2025:");
    Serial.print(gpsReliable ? "✅" : "❌");
    Serial.print(" GPS可靠:");
    Serial.print(timeoutOk ? "✅" : "❌");
    Serial.print(" 未超时:(");
    int sec = (millis() - startAttemptTime) / 1000;
    Serial.print(sec);
    Serial.print("秒)    ");
    Serial.println(getCurrentTime(true));

    bool allPass = (hasLocValid && yearOk && gpsReliable) && timeoutOk;
    if (allPass) {
      Serial.println("==== GPS全部条件满足，退出搜星循环 ====");

      break;
    }
    if (!timeoutOk) {
      Serial.print("==== 搜星 ");
      Serial.print(seacthTm / 1000);
      Serial.println(" 秒超时，强制退出 ====");
      break;
    }
    delay(1000);
  }
  Serial.println(getGpsInfoStr());
  if (closeGps) {
    setGpsEnable(false);
  }
  delay(10);
}
String lastPrintTimeStr = "";  // 存储上次打印的时间字符串（不含毫秒）

void printCurrentTime() {
  String nowStr =
    getCurrentTime(true);  // 完整时间字符串 "1970/01/01 00:00:48.679"
  // 提取秒级字符串：去掉毫秒部分（取第一个空格后的前8个字符？实际是日期+时间，要忽略毫秒）
  // 假设格式固定为 "YYYY/MM/DD HH:MM:SS.mmm"，我们需要取到秒
  int dotIndex = nowStr.indexOf('.');
  String nowSecStr =
    (dotIndex != -1) ? nowStr.substring(0, dotIndex) : nowStr;  // 去掉毫秒
  if (lastPrintTimeStr != nowSecStr) {
    Serial.println(nowStr);  // 或者只打印秒级字符串
    lastPrintTimeStr = nowSecStr;
  }
  if (!haveRightTime()) {
    // Serial.println("❌当前时间还没对时成功");
  }
}

// ==================== 系统初始化 ====================
void setup() {
  Serial.begin(115200);
  if (rtcSendCount == -1) {
    unsigned long endTm = millis() + 20000;
    while (endTm > millis()) {
      delay(1000);
      Serial.print("x");
    }
    rtcSendCount = 0;
  }

  Serial.print("setup");
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  randomSeed(analogRead(0));
  deviceName = makeDivceName();
  batterystr = readBatteryEndStr(deviceName);




  if (sendmodeFlage == 1 && strlen(needSendGpsStr) == 0) {
    meshGpsInfoFun(true);
    strcpy(needSendGpsStr, getGpsInfoStr().c_str());
  }
  initLora();
}
unsigned long num6000 = 5000;  // 暂时提前20秒开机

// ==================== 主循环 ====================
void loop() {

  Radio.IrqProcess();

  if (typeindex == FLAG_TYPE_3) {
    Radio.Sleep();  // IrqProcess已完成，安全Sleep
    if (millis() < gpsWorkStat) {
      Serial.println("准备跟踪GPS位置");
      delay(1000);  // 上报LORA需要2秒钟间隔
      return;
    }
    Serial.print("gps持续准备上报位置:");
    Serial.print(millis() - gpsWorkStat);
    Serial.print("-");
    Serial.println(gpsWorkTime);
    showOLED();
    Serial.print("获取GPS ");
    printCurrentTime();
    meshGpsInfoFun(false);
    delay(1000);
    sendLoraToMid(String(MSG_TYPE_UP_GPS) + "|" + deviceName + "|" + getGpsInfoStr());
    delay(2000);  // 上报LORA需要2秒钟间隔
    hideOLED();
    Radio.Sleep();
    if ((millis() - gpsWorkStat) > gpsWorkTime) {
      Serial.println("结束GPS上报");
      if (getGpsStatus()) {
        setGpsEnable(false);
        delay(100);
      }
      nextSendTime = 0;
      typeindex = FLAG_TYPE_0;
    } else {
      printTimeToString("延时 ", gpsWorkInterval);
      printTimeToString("还会持续", gpsWorkTime - (millis() - gpsWorkStat));
      delay(gpsWorkInterval);  // 延时1分钟
    }
    return;
  }
  if (typeindex == FLAG_TYPE_2) {
    delay(100);
    Serial.print(".");
    if (inRxEndTime < millis()) {
      Serial.print(rtcResiveIdx);
      Serial.print("-");
      Serial.print(rtcSendCount);


      // 回到普通模式，准备可以休眠
      nextSendTime = 0;
      typeindex = FLAG_TYPE_0;
      Serial.println("");
    }
    return;
  }
  if (typeindex == FLAG_TYPE_1) {
    delay(100);
    return;
  }
  if (typeindex == FLAG_TYPE_0) {
    if (rtcSendCount == 0) {
      nextSendTime = millis() - 1;
    }
    if (nextSendTime == 0) {
      nextSendTime = calculateNextSendTime(get_send_interval_ms() / 1000);
      if (timeSynFlage) {  // 接收了同步时间
        if ((nextSendTime - millis()) < get_send_interval_ms() / 2) {
          Serial.print("⚠️接收了同步时间，由于时间偏差导致又进入了上报窗口所以"
                       "要跳过这个窗口将时间后移到下一个周末");
          nextSendTime = nextSendTime + get_send_interval_ms();
        }
      }


      unsigned long waittm = nextSendTime - millis();

      printTimeToString("到上报时间还有 ", nextSendTime - millis());
      // 测试阶段多给一点时间用于烧入程序  num6000 = 10000;
      if (waittm > num6000) {
        Serial.print("距离上报时间超过 ");
        Serial.print(num6000 / 1000);
        Serial.print("秒进入睡眠");
        hideOLED();
        delay(1000);
        // 05:02|10:59
        uint64_t sleepTime = getAdjustedSleepTimeUs(waittm - num6000);
        if (sendmodeFlage == 1 && strlen(needSendGpsStr) == 0 && sleepTime > (seacthTm * 1000ULL)) {
          Serial.println("工作模式上报GPS，需要提前开启GPS");
          sleepTime = sleepTime - (seacthTm * 1000ULL);
        }
        // 根据每小时偏差补偿休眠期间的时钟漂移
        if (hourlyDriftMsTemp != 0) {
          long long sleepMs = (long long)(sleepTime / 1000ULL);
          long long driftCompMs = hourlyDriftMsTemp * sleepMs / 3600000LL;
          long long currentMs = getCurrentTimestampMs();
          long long adjustedMs = currentMs + driftCompMs;

          lastDriftCompMs = driftCompMs;

          Serial.printf("每小时偏差: %lld 毫秒\n", hourlyDriftMsTemp);
          Serial.printf("休眠时长: %llu 微秒\n", sleepTime);
          Serial.printf("休眠期间预估偏差: %lld 毫秒\n", driftCompMs);
          printTimestampMs(currentMs, "补偿前时间: ");
          setTimeFromTimestamp(adjustedMs);
          printTimestampMs(adjustedMs, "补偿后时间: ");
        }
        // esp_deep_sleep(sleepTime);
        esp_sleep_enable_timer_wakeup(sleepTime);
        Serial.println("--->即将进入深度睡眠...");
        // 计算并打印预计开机时间
        {
          uint64_t wakeUpSec = sleepTime / 1000000ULL;
          time_t now = time(nullptr);
          time_t wakeTime = now + (time_t)wakeUpSec;
          struct tm wt;
          localtime_r(&wakeTime, &wt);
          Serial.printf("预计开机时间: %02d:%02d:%02d (休眠%llu秒)\n",
                        wt.tm_hour, wt.tm_min, wt.tm_sec, wakeUpSec);
        }
        Serial.flush();
        esp_deep_sleep_start();
        Serial.println("我已经睡着了...");
      }
    }
    if (nextSendTime < millis()) {

      typeindex = FLAG_TYPE_1;
      if (sendmodeFlage == 1 && strlen(needSendGpsStr) > 0) {
        String dataStr = String(MSG_TYPE_GPS) + "|" + deviceName + "|26.52961,109.39072";
        strcpy(needSendGpsStr, "");
        sendLoraToMid(dataStr);
      } else {

        sendLoraToMid(String(MSG_TYPE_TIME) + "|" + deviceName + "|" + getCurrentTime(true));
      }
    }
  }
  printCurrentTime();
  delay(10);
}