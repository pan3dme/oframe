/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>
#include "HT_TinyGPS++.h"

// ==================== 常量定义 ====================
const char* DEVICE_NAME_PREFIX = "v4-x";
const unsigned long SEND_INTERVAL_MS = 10000;  // 发送间隔10秒
const int PACKET_TYPE_GPS = 1;                 // GPS数据包类型
const int PACKET_TYPE_TIME = 2;                // 对时数据包类型

// ==================== 全局变量 ====================
String deviceName;               // 设备名称
String gpsCoordinates;           // GPS坐标信息
char sendData[BUFFER_SIZE];      // 发送数据缓存
RadioEvents_t radioEvents;       // LoRa事件回调
int packetCount = 0;             // 数据包计数器
unsigned long lastSendTime = 0;  // 上次发送时间戳
String displayLines[4];          // OLED显示内容

// ==================== 获取当前时间字符串 ====================
String getCurrentTimeString() {
  // 使用millis()转换为相对时间格式
  unsigned long totalSeconds = millis() / 1000;
  unsigned long hours = (totalSeconds / 3600) % 24;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long seconds = totalSeconds % 60;

  return String("2026/6/17 ") + String(hours < 10 ? "0" : "") + String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes) + ":" + String(seconds < 10 ? "0" : "") + String(seconds);
}



// ==================== 读取GPS信息 ====================
void updateGpsInfo() {
  gpsCoordinates = getGpsInfoStr();
  displayLines[2] = gpsCoordinates;
}
// ==================== LoRa模块初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  initPanRadio(&radioEvents);
}
// ==================== 系统初始化 ====================
void setup() {
  delay(1000);
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // 生成设备名称并初始化显示
  deviceName = makeDivceName();
  displayLines[0] = "Device: " + deviceName;
  displayLines[1] = "";
  displayLines[2] = "Waiting GPS...";
  displayLines[3] = "LoRa Ready";

  // 初始化LoRa模块
  initLora();
  initPanGPS();
}
// ==================== 构建并发送数据包 ====================
void buildAndSendPacket(int packetType) {
  String dataStr = String(packetType) + "|" + deviceName;

  if (packetType == PACKET_TYPE_GPS) {
    dataStr += "|" + gpsCoordinates + "|" + String(packetCount);
  } else if (packetType == PACKET_TYPE_TIME) {
    // dataStr += "|" + getCurrentTimeString();
    dataStr += "|" + getCurrentTime();
  }

  // 安全拷贝到发送缓冲区
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }

  // 打印发送信息
  Serial.print("发送：");
  Serial.println(sendData);
  Serial.println(strlen(sendData));

  // 执行LoRa发送
  Radio.Send((uint8_t*)sendData, strlen(sendData));
}
// ==================== 主循环 ====================
void loop() {
  delay(1);
  gpsEncode();
  // 定时发送LoRa数据
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();
    packetCount++;

    // 随机选择发送GPS或对时信息（50%概率）
    int packetType = random(2) == 0 ? PACKET_TYPE_GPS : PACKET_TYPE_TIME;

    // 更新GPS信息（仅当发送GPS时需要）
    if (packetType == PACKET_TYPE_GPS) {
      updateGpsInfo();
    }

    // 构建并发送数据包
    buildAndSendPacket(packetType);

    // LED指示和状态显示
    openLedByNum(10, 50);
    displayLines[3] = "Sending...";
  } else {
    displayLines[3] = "LoRa Sleep";
  }

  // 处理LoRa中断
  Radio.IrqProcess();

  // 更新OLED显示
  showDisplayBy4Area(displayLines[0], displayLines[1], displayLines[2], displayLines[3]);
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