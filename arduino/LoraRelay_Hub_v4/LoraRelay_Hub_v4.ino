/*
 * 精简版：DTU（串口2）接收 JSON，提取 lorainfo 后通过 LoRa 转发
 */

#include <ArduinoJson.h>
#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <pan3dme.h>           // 提供 initPanRadio, makeDivceName 等基础函数


#define LORA_FREQ 433000000 // 433MHz 国内通用863 863   923  928   915
#define LORA_SF 11          // 扩频因子

// ==================== 全局变量 ====================
String deviceName;
char sendData[BUFFER_SIZE];
HardwareSerial *dtuSerial;

RadioEvents_t radioEvents;

// ==================== LoRa 回调 ====================
void onSendDone(void) {
  DEBUG_PRINTLN("✅ LoRa 发送完成");
}

void onSendTimeout(void) {
  DEBUG_PRINTLN("❌ LoRa 发送超时");
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  // 仅简单打印接收到的数据（如需处理可扩展）
  if (size >= BUFFER_SIZE) return;
  char buf[BUFFER_SIZE];
  memcpy(buf, payload, size);
  buf[size] = '\0';
  DEBUG_PRINT("接收到 LoRa 数据: ");
  DEBUG_PRINTLN(buf);
}

void OnRxTimeout(void) {
  DEBUG_PRINTLN("⚠️ LoRa 接收超时");
}

void OnRxError(void) {
  DEBUG_PRINTLN("❌ LoRa 接收错误");
}

// ==================== LoRa 初始化 ====================
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  radioEvents.RxDone = OnRxDone;
  radioEvents.RxTimeout = OnRxTimeout;
  radioEvents.RxError = OnRxError;

  // 使用默认功率（22dBm），如需调整可改
  initPanRadio(&radioEvents, 22,433000000,10);
}

// ==================== 发送 LoRa 数据 ====================
void sendLoraToSave(String dataStr) {
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    DEBUG_PRINTLN("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }
  DEBUG_PRINT("发送 LoRa: ");
  DEBUG_PRINTLN(sendData);
  Radio.Send((uint8_t *)sendData, strlen(sendData));
  delay(100);
}

// ==================== 从 DTU 接收并解析 JSON ====================
void receiveDtuData() {
  if (dtuSerial->available() <= 0) return;

  String raw = "";
  int rawLen = 0;
  while (dtuSerial->available() > 0 && rawLen < 1024) {
    raw += (char)dtuSerial->read();
    rawLen++;
    delay(2);
  }
  // 丢弃超出部分
  while (dtuSerial->available() > 0) dtuSerial->read();
  dtuSerial->flush();

  Serial.print("DTU 返回: ");
  Serial.println(raw);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, raw);
  if (error) {
    Serial.print("JSON 解析失败: ");
    Serial.println(error.f_str());
    return;
  }

  // ----- 判断是否存在 params.lorainfo -----
  if (!doc.containsKey("params")) {
    Serial.println("错误：缺少 params 对象");
    return;
  }
  if (!doc["params"].containsKey("lorainfo")) {
    Serial.println("错误：params 中缺少 lorainfo 字段");
    return;
  }

  String lorainfo = doc["params"]["lorainfo"].as<String>();
  Serial.print("lorainfo 存在，值为: ");
  Serial.println(lorainfo);

  // 转发给 LoRa
  sendLoraToSave(lorainfo);
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  deviceName = makeDivceName();          // 从 pan3dme 获取设备名（若无需可注释）
  Serial.println("设备名: " + deviceName);

#if defined(WIFI_LORA_32_V4)
  // 根据硬件调整引脚，此处使用 V4 默认 GPS 引脚（RX=38, TX=39）
  // Serial2.begin(115200, SERIAL_8N1, 38, 39);
  Serial2.begin(115200, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  dtuSerial = &Serial2;
  Serial.println("✅ DTU 串口初始化完成 (Serial2)");
#endif

  initLora();
  Serial.println("✅ 系统启动完成");
}

// ==================== loop ====================
void loop() {
  Radio.IrqProcess();        // 处理 LoRa 中断事件
  receiveDtuData();          // 检查并处理 DTU 数据
  delay(1000);               // 每1秒检查一次，可根据需求调整
}