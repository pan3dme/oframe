/*
 * HelTec ESP32 LoRa 接收端程序 (牛羊GPS定位)
 * 功能：LoRa接收数据 + GPS采集 + WiFi获取网络时间 + BLE收到指令后发送数据
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include "HT_TinyGPS++.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <time.h>
#include <ArduinoJson.h>
#include <pan3dme.h>

// BLE全局对象
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool needSync = false;

static RadioEvents_t RadioEvents;

// 显示缓冲区
String displayBuf[4] = { "", "", "", "" };

// 数据队列配置
#define DATA_MAX_COUNT 99
String dataArray[DATA_MAX_COUNT];
int dataCount = 0;
int receiveCount = 0;
String deviceName = "v4-x";

// LoRa接收缓冲区
char loraStr[BUFFER_SIZE];

// LoRa状态标志
bool needPlaLed = false;
bool loraReceivedFlag = false;
volatile int16_t lastRssi = 0;
volatile int8_t lastSnr = 0;
volatile uint16_t lastPayloadSize = 0;


// BLE服务器回调
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("✅ 小程序已连接");
  }
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    needSync = false;
    Serial.println("❌ 断开连接 | 同步已关闭");
    pServer->startAdvertising();
  }
};

// BLE特征值回调 (解析JSON指令)
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("📥 收到蓝牙指令：");
    Serial.println(rxValue);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, rxValue);

    if (!error && doc.containsKey("syncing")) {
      needSync = doc["syncing"].as<bool>();
      Serial.println(needSync ? "✅ 同步已开启" : "⏹️ 同步已关闭");
    }
  }
};

// 添加数据到队列 (满时覆盖最旧数据)
void addDataToQueue(String data) {
  if (dataCount < DATA_MAX_COUNT) {
    dataArray[dataCount++] = data;
  } else {
    for (int i = 0; i < DATA_MAX_COUNT - 1; i++) {
      dataArray[i] = dataArray[i + 1];
    }
    dataArray[DATA_MAX_COUNT - 1] = data;
  }
}

// 取出并删除队列头部数据 (FIFO)
String getAndRemoveFirstData() {
  if (dataCount == 0) return "";
  String first = dataArray[0];
  for (int i = 0; i < dataCount - 1; i++) {
    dataArray[i] = dataArray[i + 1];
  }
  dataArray[--dataCount] = "";
  return first;
}

// 初始化BLE服务
void initBLE() {
  static MyServerCallbacks serverCallbacks;
  static MyCallbacks charCallbacks;
  BLECallbacks bleCallbacks = initBLEFun(deviceName, &serverCallbacks, &charCallbacks);
  pServer = bleCallbacks.pServer;
  pCharacteristic = bleCallbacks.pCharacteristic;
}




// LoRa接收回调 (仅做数据拷贝，耗时操作在主循环处理)
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    memcpy(loraStr, payload, size);
    loraStr[size] = '\0';
    lastRssi = rssi;
    lastSnr = snr;
    lastPayloadSize = size;
    loraReceivedFlag = true;
    needPlaLed = true;
    receiveCount++;
  } else {
    loraStr[0] = '\0';
    lastPayloadSize = 0;
  }
  Radio.Rx(0);
}

// 初始化LoRa模块
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  initPanRadio(&RadioEvents);
  Radio.Rx(0);  // 重新开启接收
}


// 系统初始化
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);

  deviceName = makeDivceName();
  displayBuf[0] = "id:" + deviceName + " rec";

  initLibWifi();
  initRadio();
  initBLE();

  Serial.println("✅ 系统启动完成");
}
// 主循环
unsigned long lastDisplayUpdate = 0;

void loop() {
  Radio.IrqProcess();

  // 处理LoRa接收数据
  if (loraReceivedFlag) {
    loraReceivedFlag = false;
    Serial.print("  --> ");
    Serial.print("Rssi");
    Serial.print(lastRssi);
    Serial.print("  Snr");
    Serial.println(lastSnr);

    Serial.print("Received LoRa: ");
    Serial.println(loraStr);
    displayBuf[3] = String(loraStr).substring(0, 15);

    if (lastPayloadSize > 0) {

      StaticJsonDocument<200> doc;
      doc["rssi"] = (int)lastRssi;
      doc["snr"] = (int)lastSnr;
      doc["info"] = loraStr;
      doc["upDateDevice"] = deviceName;
      doc["time"] = getCurrentTime();

      String jsonData;
      serializeJson(doc, jsonData);
      addDataToQueue(jsonData);
      lastPayloadSize = 0;
    }
  }

  // GPS数据解析
  if (Serial1.available() > 0) {
    gpsEncode();
  }

  // LED提示
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(5, 50);
  }

  // 更新显示计数
  displayBuf[0] = "id:" + deviceName + " rec" + String(receiveCount);

  // BLE数据发送
  if (deviceConnected && needSync && dataCount > 0) {
    String data = getAndRemoveFirstData();
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();

    Serial.print("✅ 同步发送：");
    Serial.println(data);
    Serial.print("📊 剩余：");
    Serial.println(dataCount);
  }

  // 降低OLED刷新频率 (每500ms更新一次)
  unsigned long now = millis();
  if (now - lastDisplayUpdate >= 500) {
    showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);
    lastDisplayUpdate = now;
  }

  delay(100);
}