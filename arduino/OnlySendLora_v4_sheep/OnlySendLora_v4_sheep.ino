/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"
#include <Preferences.h>
Preferences prefs;

// ==================== 常量定义 ====================




// ==================== 全局变量 ====================
String deviceName;           // 设备名称
String gpsCoordinates;       // GPS坐标信息
char sendData[BUFFER_SIZE];  // 发送数据缓存
RadioEvents_t radioEvents;   // LoRa事件回调
int packetCount = 0;         // 数据包计数器

String displayBuf[4] = { "", "", "", "" };

// LoRa发射时间管理
int deviceIndex = -1;            // 当前设备索引（从pan3dme获取）
int totalDevices = 0;            // 设备总数（从pan3dme获取）
unsigned long nextSendTime = 0;  // 下次发送时间点（millis）

bool needOpenGps = true;
const int typeList[] = { MSG_TYPE_GPS, MSG_TYPE_TIME, MSG_TYPE_BATTERY };
int needpacketType = 0;


// LoRa接收窗口状态
bool inRxMode = false;           // 当前是否处于接收模式
unsigned long rxStartTime = 0;   // RX窗口开始的millis()
bool didSend = false;            // 本周期是否已发送（控制RX窗口和休眠）
bool rxWindowDone = false;       // 本周期RX窗口是否已结束（防止收到消息后重新进入）
char rxBuffer[BUFFER_SIZE + 1];  // 接收数据缓存
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
  // 计算从当天0点开始，已经经历了多少个完整的周期
  unsigned long cyclesPassed = currentSeconds / intervalSeconds;

  // 计算“上一个”属于我的发送时隙的绝对时间点
  unsigned long lastTargetSeconds = cyclesPassed * intervalSeconds + mySlotOffset;

  long secondsDiff = 0;

  // 判断“上一个”时隙是否已经过去
  if (lastTargetSeconds < currentSeconds) {
    // 如果已过，那么下一个时隙就是再等一个完整的周期
    secondsDiff = intervalSeconds - (currentSeconds - lastTargetSeconds);
  } else {
    // 如果还没到（说明当前时间正好在周期的最开始部分），下一个时隙就是它
    secondsDiff = lastTargetSeconds - currentSeconds;
  }

  // 确保等待时间不为负数
  if (secondsDiff < 0) {
    secondsDiff += intervalSeconds;
  }

  unsigned long delayMillis = secondsDiff * 1000;
  // Serial.printf("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu ms\n", timeStr.c_str(), deviceIndex, slotDuration, delayMillis);
  unsigned long minutes = delayMillis / 60000;           // 取整分钟
  unsigned long seconds = (delayMillis % 60000) / 1000;  // 取剩余秒数（去掉毫秒部分）

  Serial.printf("当前时间: %s, 设备%d, 时隙%.2f秒, 延迟%lu分%lu秒\n",
                timeStr.c_str(), deviceIndex, slotDuration, minutes, seconds);

  return millis() + delayMillis;
}


// ==================== LoRa模块初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = onRxDone;
  radioEvents.RxTimeout = onRxTimeout;
  initPanRadio(&radioEvents);
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

  // 关闭检测电路（V3 HIGH 关闭，V4 LOW 关闭）
  digitalWrite(VBAT_CTRL_PIN, isV4 ? LOW : HIGH);

  // 分压系数 5.35（实测校准：785mV × 5.35 ≈ 4.2V 满电）
  float batteryVoltage = mvAvg * 5.35 / 1000.0;

  Serial.printf("[BAT] raw=%.0f mv=%.0f V=%.2f\n", rawAvg, mvAvg, batteryVoltage);

  int soc = map(batteryVoltage * 1000, 3000, 4200, 0, 100);
  soc = constrain(soc, 0, 100);
  float socRatio = soc / 100.0;

  // 格式: soc|adc_raw|adc_mV|voltage

  return String(socRatio, 1) + "|" + String(batteryVoltage, 1);
}

// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {

  String dataStr = String(packetType) + "|" + deviceName;

  dataStr += "|" + getGpsInfoStr() + "|" + batterystr;


  // 安全拷贝到发送缓冲区
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }

  // 打印发送信息
  Serial.print("发送：");
  Serial.print(sendData);
  Serial.print("  len:");
  Serial.println(strlen(sendData));

  // 执行LoRa发送
  Radio.Send((uint8_t*)sendData, strlen(sendData));
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

// ==================== LoRa接收完成回调 ====================
void onRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  Radio.Sleep();
}

// ==================== LoRa接收超时回调 ====================
void onRxTimeout(void) {
  Radio.Sleep();
  inRxMode = false;
  Serial.println("⏹ RX超时，结束接收... " + getCurrentTime());
}


// ==================== 系统初始化 ====================

void setup() {

  Serial.begin(115200);
  delay(1000);
  Serial.print("setup");
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  deviceName = makeDivceName();
  initPanGPS();
  unsigned long startAttemptTime = millis();
  Serial.print("有gps模块 设置2分钟跳出");
  int skipnum = 0;
  while (true) {
    delay(1000);
    gpsEncode();
    // 逐个计算条件
    bool hasLocValid = gps.location.isValid();
    bool yearOk = (gps.date.year() > 2025);
    bool gpsReliable = isReliableGPS();
    bool timeoutOk = (millis() - startAttemptTime < 120000);

    // 全部打印出来
    Serial.print("定位有效:");
    Serial.print(hasLocValid ? "Y" : "N");
    Serial.print(" 年份>2025:");
    Serial.print(yearOk ? "Y" : "N");
    Serial.print(" GPS可靠:");
    Serial.print(gpsReliable ? "Y" : "N");
    Serial.print(" 未超时:");
    Serial.println(timeoutOk ? "Y" : "N");

    // 原始总条件
    bool allPass = (hasLocValid && yearOk && gpsReliable) && timeoutOk;
    if (allPass) {
      Serial.println("==== GPS全部条件满足，退出搜星循环 ====");
      break;
    }

    // 超时直接跳出
    if (!timeoutOk) {
      Serial.println("==== 搜星120秒超时，强制退出 ====");
      break;
    }

    Serial.print(".");
    Serial.println(getCurrentTime());
    showDisplayBy4Area("count", String(skipnum++), getCurrentTime(), "gps...");
  }
  int i = 0;
  while (i++ < 10) {
    delay(500);
    gpsEncode();
    Serial.print("延时：");
    Serial.println(getCurrentTime());
  }
  setGpsEnable(false);
  delay(1000);


  unsigned long num60000 = 120000;  //设定1分钟
  unsigned long nextSendTime = calculateNextSendTime(SEND_INTERVAL_MS / 1000);
  unsigned long waittm = nextSendTime - millis();
  if (waittm > num60000) {
    Serial.println("超过60秒 在发送前1分钟 就重新开机");
    printTimeToString("到上报时间还有 ", nextSendTime - millis());
    uint64_t sleepTime = (uint64_t)(waittm - num60000) * 1000ULL;
    esp_deep_sleep(sleepTime);

  } else {
    Serial.println("小于60秒 那就延时到发射时间准备发送消息");
    delay(10);
    analogReadResolution(12);
    delay(10);
    pinMode(VBAT_CTRL_PIN, OUTPUT);
    delay(10);
    digitalWrite(VBAT_CTRL_PIN, HIGH);
    delay(10);

    batterystr = readBatteryEndStr();
    Serial.print("batterystr");
    Serial.println(batterystr);


    initLora();
    delay(1000);
    //再算一次发送时间
    printTimeToString("到上报时间还有 ", nextSendTime - millis());
    delay(nextSendTime - millis());
    Serial.println(getCurrentTime());
    // buildAndSendPacket(typeList[random(3)]);
    buildAndSendPacket(MSG_TYPE_GPS);
    hideOLED();
    digitalWrite(VBAT_CTRL_PIN, LOW);
    Serial.println("已上报信息 下次发送前1分钟开机");
    uint64_t sleepTime = (uint64_t)(SEND_INTERVAL_MS - num60000) * 1000ULL;
    esp_deep_sleep(sleepTime);
  }
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

// ==================== 主循环 ====================

void loop() {
  delay(1000);
}
