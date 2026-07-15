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
// LoRa接收回调（仅拷贝数据，耗时操作在主循环处理）
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    memcpy(loraStr, payload, size);
    loraStr[size] = '\0';
    Serial.println("");
    Serial.print(" ROLA -：");
    Serial.println(loraStr);
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
  if (packetType == MSG_TYPE_TIME) {
    dataStr += "|" + getCurrentTime() + "|" + batterystr;
  } else {
    dataStr += "|" + getGpsInfoStr() + "|" + batterystr;
  }



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

// ==================== LoRa发送完成回调 ====================
void onSendDone(void) {
  Radio.Sleep();
  Serial.println("✅ 发送完成");
}

// ==================== LoRa发送超时回调 ====================
void onSendTimeout(void) {
  Radio.Sleep();
  Serial.println("❌ 发送超时");
}
bool mustOpenGps = true;
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
bool meshGpsInfoFun() {
  initPanGPS();
  unsigned long startAttemptTime = millis();
  int skipnum = 0;
  while (true) {

    gpsEncode();
    bool hasLocValid = gps.location.isValid();
    bool yearOk = (gps.date.year() > 2025);
    bool gpsReliable = isReliableGPS();
    bool timeoutOk = (millis() - startAttemptTime < 120000);

    // Serial.print(".");
    // Serial.println(getCurrentTime());
    showDisplayBy4Area(deviceName, getGpsInfoStr(), getCurrentTime(), String(skipnum++));

    // Serial.print("定位有效:");
    // Serial.print(hasLocValid ? "✅" : "❌");
    // Serial.print(" 年份>2025:");
    // Serial.print(yearOk ? "✅" : "❌");
    // Serial.print(" GPS可靠:");
    // Serial.print(gpsReliable ? "✅" : "❌");
    // Serial.print(" 未超时:");
    // Serial.println(timeoutOk ? "✅" : "❌");

    bool allPass = (hasLocValid && yearOk && gpsReliable) && timeoutOk;
    if (allPass) {
      Serial.println("==== GPS全部条件满足，退出搜星循环 ====");
      break;
    }
    if (!timeoutOk) {
      Serial.println("==== 搜星120秒超时，强制退出 ====");
      return false;
      break;
    }
    delay(10);
  }

  return true;
}
String lastPrintTime = "";
void printCurrentTime() {
  if (lastPrintTime != getCurrentTime()) {
    Serial.println(getCurrentTime());
    lastPrintTime = getCurrentTime();
  }
}

// ==================== 系统初始化 ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
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
//判断是不是接收窗口
void isRxWindowTime() {

  unsigned long intervalSec = SEND_INTERVAL_MS / 1000;
  bool canRx = (intervalSec > RX_WINDOW_SECONDS + 1);
  unsigned long rxWindowMs = canRx ? RX_WINDOW_SECONDS * 1000 : 0;
  String timeNow = getCurrentTime();
  int h, m, s;
  sscanf(timeNow.c_str(), "%*d/%*d/%*d %d:%d:%d", &h, &m, &s);
  unsigned long timeOfDaySec = h * 3600UL + m * 60UL + s;
  unsigned long nextCycleBoundaryMs = millis() + (intervalSec - timeOfDaySec % intervalSec) * 1000UL;
  unsigned long rxStartMs = nextCycleBoundaryMs - rxWindowMs;
  if (millis() >= rxStartMs && millis() < nextCycleBoundaryMs) {
    Radio.Rx(0);
    Serial.print("开始接收窗口");
    printCurrentTime();
    unsigned long ds = nextCycleBoundaryMs - millis();
    printTimeToString("等待接收时间", ds);
    while (millis() < nextCycleBoundaryMs) {
      Radio.IrqProcess();
      delay(1000);
      Serial.print(".");
    }
    Radio.Sleep();
    Serial.println("");
    Serial.print("结束接收窗口");
    printCurrentTime();
  }
}
// ==================== 主循环 ====================
void loop() {

  Radio.IrqProcess();

  if (nextSendTime == 0) {
    //第一次
    nextSendTime = calculateNextSendTime(SEND_INTERVAL_MS / 1000);
    unsigned long waittm = nextSendTime - millis();
    printTimeToString("到上报时间还有 ", nextSendTime - millis());
  }

  isRxWindowTime();

  if (nextSendTime < millis()) {
    // MSG_TYPE_TIME,MSG_TYPE_GPS
    buildAndSendPacket(MSG_TYPE_TIME);
    delay(1000);
    nextSendTime = calculateNextSendTime(SEND_INTERVAL_MS / 1000);
  }


  delay(1000);
}