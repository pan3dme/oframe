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
unsigned long gpsWorkStat = 0;
int typeindex = 0;  // 0空闲 1发送中 2接收等待 3GPS搜星

RTC_DATA_ATTR unsigned long rtcSendCount = 0;
RTC_DATA_ATTR bool isFristOpenGps = true;  //标记是否还在第一次开启GPS
RTC_DATA_ATTR int roundTime = 0;           //默认上报周末使用系统配置，如果有接收到就按数字算新的周末
RadioEvents_t radioEvents;                 // LoRa事件回调

unsigned long get_send_interval_ms() {
  if (roundTime == 0) {
    return SEND_INTERVAL_MS;
  } else {
    return roundTime;
  }
}
float getSlotDuration() {
  return get_send_interval_ms() / 60 / 1000;
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
    (unsigned long)(deviceIndex * getSlotDuration());  // 我在周期内的偏移量

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
                timeStr.c_str(), deviceIndex, getSlotDuration(), minutes, seconds);

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
  if (isV4 && rtcSendCount % 2 == 0) {
    initPanRadio(&radioEvents, 28);
  } else {
    initPanRadio(&radioEvents, TX_POWER);
  }
}
void OnRxTimeout(void) {
  Serial.println("⚠️ Radio接收超时!");
}

void OnRxError(void) {
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
  Serial.print(" ROLA -：");
  Serial.println(buf);

  // 检查是否为GPS高频指令（5|v4-6,...）且目标是当前设备
  String infoStr(buf);
  int firstPipeIndex = infoStr.indexOf('|');
  if (firstPipeIndex <= 0) {
    return;
  }

  int messageType = infoStr.substring(0, firstPipeIndex).toInt();
  if (messageType == MSG_TYPE_UP_GPS && isMyDeviceInList(infoStr, deviceName)) {
    Serial.println("✅是当前设备，标记GPS搜星");
    typeindex = 3;
    int lastPipe = infoStr.lastIndexOf('|');
    if (lastPipe != -1) {
      String lastPart = infoStr.substring(lastPipe + 1);
      int value = lastPart.toInt();
      gpsWorkStat = millis();
      gpsWorkTime = value * 60 * 1000;
    }
  }
  if (messageType == MSG_TYPE_CHANGE_ROUND && isMyDeviceInList(infoStr, deviceName)) {
    int lastPipe = infoStr.lastIndexOf('|');
    if (lastPipe != -1) {
      String lastPart = infoStr.substring(lastPipe + 1);
      int value = lastPart.toInt();
      roundTime = value * 60 * 1000;
    }
  }



  if (messageType == MSG_TYPE_SYN_TIME) {
    int secondPipeIndex = infoStr.indexOf('|', firstPipeIndex + 1);
    String timeStr = infoStr.substring(secondPipeIndex + 1);
    timeStr = timeStr.substring(0, timeStr.indexOf('|'));

    if (!haveRightTime()) {
      Serial.print("✅本机时间无效 更新ROLA同步时间 ");
    } else {
      long long diff_ms = mathTimeDiffmstimeFromLora(timeStr);
      printTimeToString("✅ 收到对时信息  : 和本机时间差", diff_ms);
    }
    timeSynFlage = true;
    setTimeFromLora(timeStr);
    Serial.println(timeStr);
  }
}


// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {
  String dataStr = String(packetType) + "|" + deviceName;
  if (packetType == MSG_TYPE_TIME || packetType == MSG_TYPE_SYN_TIME) {
    dataStr += "|" + getCurrentTime(true) + "|" + batterystr;
  } else if (packetType == MSG_TYPE_GPS) {
    dataStr += "|" + getGpsInfoStr() + "|" + batterystr;
  }

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
//结果接收窗口
unsigned long inRxEndTime = 0;
// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  // Radio.Sleep();
  Serial.print("✅ 发送完成");
  if (typeindex == 1) {
    Serial.println("定时上报信息");
    inRxEndTime = millis() + 5000;  //5秒后结束接收窗口
    typeindex = 2;
    Radio.Rx(0);
  } else if (typeindex == 3) {
    Serial.println("Gps上报完成");
  }
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {
  Radio.Sleep();
  Serial.println("❌ 发送超时");
  typeindex = 0;
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
    bool timeoutOk = (millis() - startAttemptTime < 180000);
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
      isFristOpenGps = false;
      break;
    }
    if (!timeoutOk) {
      Serial.println("==== 搜星120秒超时，强制退出 ====");
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
  String nowStr = getCurrentTime(true);  // 完整时间字符串 "1970/01/01 00:00:48.679"
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
  delay(100);
  Serial.print("setup");
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  deviceName = makeDivceName();
  if (haveRightTime()) {
    Serial.print("✅已有GPS时间");
    Serial.println(getCurrentTime(true));
  } else {
    Serial.print("❌板子还没有时间");
    Serial.println(getCurrentTime(true));
    isFristOpenGps = true;
  }
  // 必须开GPS
  if (isFristOpenGps) {
    meshGpsInfoFun();
  } else {
    Serial.print("✅不打开GPS，也就是现在只有时间");
  }
  batterystr = readBatteryEndStr(deviceName);

  initLora();
}
unsigned long num6000 = 10000;  //暂时提前10秒开机
bool sendFlagType = false;
// ==================== 主循环 ====================
void loop() {

  Radio.IrqProcess();

  if (typeindex == 3) {
    Serial.print("gps持续准备上报位置:");
    Serial.print(millis() - gpsWorkStat);
    Serial.print("-");
    Serial.println(gpsWorkTime);
    Radio.Sleep();  // IrqProcess已完成，安全Sleep
    showOLED();
    delay(1000);
    Serial.print("获取GPS");
    printCurrentTime();
    meshGpsInfoFun(false);
    buildAndSendPacket(MSG_TYPE_GPS);
    delay(2000);  //上报LORA需要2秒钟间隔
    hideOLED();
    Radio.Sleep();
    if ((millis() - gpsWorkStat) > gpsWorkTime) {
      Serial.println("结束GPS上报");
      if (getGpsStatus()) {
        setGpsEnable(false);
        delay(100);
      }
      nextSendTime = 0;
      typeindex = 0;
    } else {
      printTimeToString("延时1分钟再上报GPS信息 还会持续", gpsWorkTime - (millis() - gpsWorkStat));
      delay(60 * 1000);  //延时1分钟
    }
    return;
  }
  if (typeindex == 2) {
    delay(100);
    Serial.print(".");
    if (inRxEndTime < millis()) {
      // 回到普通模式，准备可以休眠
      nextSendTime = 0;
      typeindex = 0;
      Serial.println("");
    }
    return;
  }
  if (typeindex == 1) {
    delay(100);
    return;
  }
  if (typeindex == 0) {
    if (nextSendTime == 0) {
      nextSendTime = calculateNextSendTime(get_send_interval_ms() / 1000);
      if (timeSynFlage) {  //接收了同步时间
        if ((nextSendTime - millis()) < num6000) {
          Serial.print("❌接收了同步时间，由于时间偏差导致又进入了上报窗口所以要跳过这个窗口将时间后移到下一个周末");
          nextSendTime = nextSendTime + get_send_interval_ms();
        }
      }

      if (isFristOpenGps && !sendFlagType) {
        //没有GPS授时就设定一次开机上报，数据基本是错误的，只做为上报链路测试
        sendFlagType = true;
        nextSendTime = millis() + num6000;
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
        uint64_t sleepTime = (uint64_t)(waittm - num6000) * 1000ULL;
        // esp_deep_sleep(sleepTime);
        esp_sleep_enable_timer_wakeup(sleepTime);
        Serial.println("--->即将进入深度睡眠...");
        Serial.flush();
        esp_deep_sleep_start();
        Serial.println("我已经睡着了...");
      }
    }
    if (nextSendTime < millis()) {
      typeindex = 1;

      if (rtcSendCount == 0 && getGpsInfoStr() != "0.00000,0.00000") {
        //第一次并有GPS时就发送GPS
        buildAndSendPacket(MSG_TYPE_GPS);
      } else {
        buildAndSendPacket(MSG_TYPE_TIME);
      }
    }
  }
  printCurrentTime();
  delay(10);
}