/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"

// ==================== 全局变量 ====================
String deviceName;           // 设备名称
char sendData[BUFFER_SIZE];  // 发送数据缓存
RadioEvents_t radioEvents;   // LoRa事件回调
char loraStr[BUFFER_SIZE];
// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）

String batterystr = "";

bool inRxWindow = false;
int typeindex = 0;  //0普通 1发送 2接收

RTC_DATA_ATTR unsigned long rtcSendCount = 0;
RTC_DATA_ATTR bool mustOpenGps = true;
RTC_DATA_ATTR bool isRightGpsinfo = false;

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
  String timeStr = getCurrentTime();
  int hour = 0, minute = 0, second = 0;
  sscanf(timeStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &hour, &minute, &second);
  unsigned long currentSeconds = hour * 3600 + minute * 60 + second;

  // 2. 计算基础参数
  unsigned long mySlotOffset = (unsigned long)(deviceIndex * slotDuration);  // 我在周期内的偏移量

  // 3. 核心修复逻辑：计算到下一个时隙的等待时间
  unsigned long cyclesPassed = currentSeconds / intervalSeconds;
  unsigned long lastTargetSeconds = cyclesPassed * intervalSeconds + mySlotOffset;

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
                timeStr.c_str(), deviceIndex, slotDuration, minutes, seconds);

  return millis() + delayMillis;
}

// ==================== LoRa模块初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = OnRxDone;
  radioEvents.RxTimeout = OnRxTimeout;
  radioEvents.RxError = OnRxError;
  // 删除了 RxDone 和 RxTimeout 回调（未使用）
  initPanRadio(&radioEvents);
}
void OnRxTimeout(void) {
  Serial.println("⚠️ Radio接收超时!");
}

void OnRxError(void) {
  Serial.println("❌ Radio接收错误!");
}
String extractDeviceIdFromInfo(String infoStr) {
  int first = infoStr.indexOf('|');
  if (first == -1) return "";
  int second = infoStr.indexOf('|', first + 1);
  if (second == -1) return infoStr.substring(first + 1);
  return infoStr.substring(first + 1, second);
}
bool isMyDeviceInList(String infoStr, String targetDevice) {
  String id = extractDeviceIdFromInfo(infoStr);
  return id.equals(targetDevice);
}
// LoRa接收回调（仅拷贝数据，耗时操作在主循环处理）
void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    memcpy(loraStr, payload, size);
    loraStr[size] = '\0';
    Serial.println("");
    Serial.print(" ROLA -：");
    Serial.println(loraStr);
    // 1|v3-18|2026/07/16 12:35:55.007

    int firstPipeIndex = String(loraStr).indexOf('|');
    if (firstPipeIndex > 0) {
      int messageType = String(loraStr).substring(0, firstPipeIndex).toInt();
      if (messageType == MSG_TYPE_SYN_TIME) {
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          String timeStr = String(loraStr).substring(secondPipeIndex + 1);
          timeStr = timeStr.substring(0, timeStr.indexOf('|'));
          Serial.print("⏰ 收到对时信息: ");
          Serial.println(timeStr);
          // setTimeFromLora(timeStr);
        }
      }
      //5|v4-6,v4-6
      if (messageType == MSG_TYPE_UP_GPS) {
        bool inList = isMyDeviceInList(String(loraStr), deviceName);
        if (inList) {
          Serial.println("✅是当前设备，执行更新GPS信息");
          typeindex = 3;
          Radio.Sleep();
        }
      }
    } else {
    }
  }
}

String readBatteryEndStr() {
  // V4 与 V3 控制逻辑相反：V3 LOW 开启，V4 HIGH 开启
  bool isV4 = deviceName.startsWith("v4-");
  digitalWrite(VBAT_CTRL_PIN, isV4 ? HIGH : LOW);
  delay(100);

  const int samples = 10;
  long rawSum = 0;
  long mvSum = 0;
  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(VBAT_READ_PIN);
    mvSum += analogReadMilliVolts(VBAT_READ_PIN);
    delay(1);
  }
  float rawAvg = (float)rawSum / samples;
  float mvAvg = (float)mvSum / samples;

  digitalWrite(VBAT_CTRL_PIN, isV4 ? LOW : HIGH);

  float batteryVoltage = mvAvg * 5.35 / 1000.0;

  Serial.printf("[BAT] raw=%.0f mv=%.0f V=%.2f\n", rawAvg, mvAvg, batteryVoltage);

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 100);
  soc = constrain(soc, 0, 100);
  float socRatio = soc / 100.0;

  return String(socRatio, 1) + "|" + String(batteryVoltage, 1);
}

// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {
  String dataStr = String(packetType) + "|" + deviceName;
  if (packetType == MSG_TYPE_TIME || packetType == MSG_TYPE_SYN_TIME) {
    dataStr += "|" + getCurrentTime() + "|" + batterystr;
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

  Radio.Send((uint8_t*)sendData, strlen(sendData));
  delay(100);
}
unsigned long finishTime = 0;
// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  // Radio.Sleep();
  Serial.print("✅ 发送完成");
  if (typeindex == 1) {
    Serial.println("定时上报信息");
    finishTime = millis() + 2000;
    typeindex = 2;
    Radio.Rx(0);
  } else if (typeindex == 3) {
    Serial.println("Gps上按了完成");
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
void meshGpsInfoFun() {
  initPanGPS();
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
    showDisplayBy4Area(deviceName, getGpsInfoStr(), getCurrentTime(), String(skipnum++));
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
    Serial.println(getCurrentTime());

    bool allPass = (hasLocValid && yearOk && gpsReliable) && timeoutOk;
    isRightGpsinfo = gpsReliable;
    if (allPass) {
      Serial.println("==== GPS全部条件满足，退出搜星循环 ====");
      mustOpenGps = false;
      break;
    }
    if (!timeoutOk) {
      Serial.println("==== 搜星120秒超时，强制退出 ====");
      break;
    }
    delay(1000);
  }

  Serial.println(getGpsInfoStr());
  delay(1000);
  setGpsEnable(false);
  delay(1000);
}
String lastPrintTimeStr = "";  // 存储上次打印的时间字符串（不含毫秒）

void printCurrentTime() {
  String nowStr = getCurrentTime();  // 完整时间字符串 "1970/01/01 00:00:48.679"
  // 提取秒级字符串：去掉毫秒部分（取第一个空格后的前8个字符？实际是日期+时间，要忽略毫秒）
  // 假设格式固定为 "YYYY/MM/DD HH:MM:SS.mmm"，我们需要取到秒
  int dotIndex = nowStr.indexOf('.');
  String nowSecStr = (dotIndex != -1) ? nowStr.substring(0, dotIndex) : nowStr;  // 去掉毫秒
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
    Serial.println(getCurrentTime());
  } else {
    Serial.print("❌板子还没有时间");
    Serial.println(getCurrentTime());
    mustOpenGps = true;
  }
  //必须开GPS
  if (mustOpenGps) {
    meshGpsInfoFun();
  } else {
    Serial.print("✅不打开GPS，也就是现在只有时间");
  }
  initLora();
  //测试电量
  analogReadResolution(12);
  delay(10);
  pinMode(VBAT_CTRL_PIN, OUTPUT);
  delay(10);
  digitalWrite(VBAT_CTRL_PIN, HIGH);
  delay(10);
  batterystr = readBatteryEndStr();
  digitalWrite(VBAT_CTRL_PIN, LOW);
  pinMode(VBAT_CTRL_PIN, INPUT_PULLDOWN);
  Serial.print("batterystr");
  Serial.println(batterystr);
}

// ==================== 主循环 ====================
void loop() {
  Radio.IrqProcess();
  if (typeindex == 3) {
    delay(1000);
    Serial.print("获取GPS");
    printCurrentTime();
    meshGpsInfoFun();
    buildAndSendPacket(MSG_TYPE_GPS);
    delay(2000);
    Radio.Sleep();
    nextSendTime = 0;
    typeindex = 0;
    return;
  }
  if (typeindex == 2) {
    delay(10);
    Serial.print(".");
    if (finishTime < millis()) {
      //回到普通模式，准备可以休眠
      nextSendTime = 0;
      typeindex = 0;
    }
    return;
  }
  if (typeindex == 1) {
    delay(10);
    return;
  }
  if (typeindex == 0) {
    if (nextSendTime == 0) {
      //第一次
      nextSendTime = calculateNextSendTime(SEND_INTERVAL_MS / 1000);
      unsigned long waittm = nextSendTime - millis();
      printTimeToString("到上报时间还有 ", nextSendTime - millis());
      //测试阶段多给一点时间用于烧入程序  num6000 = 10000;
      unsigned long num6000 = 60000;
      if (waittm > num6000) {
        Serial.print("距离上报时间超过 ");
        Serial.print(num6000 / 1000);
        Serial.print("秒进入睡眠");
        delay(1000);
        uint64_t sleepTime = (uint64_t)(waittm - num6000) * 1000ULL;
        esp_deep_sleep(sleepTime);
      }
    }
    if (nextSendTime < millis()) {
      typeindex = 1;
      buildAndSendPacket(MSG_TYPE_TIME);
    }
  }
  printCurrentTime();
  delay(10);
}