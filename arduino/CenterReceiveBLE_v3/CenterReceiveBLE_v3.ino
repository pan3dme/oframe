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

String deviceName = "x-x";


// LoRa接收缓冲区
char loraStr[BUFFER_SIZE];

// LoRa对时发送缓冲区
char timeSyncSendBuf[BUFFER_SIZE];
bool needSendTimeSync = false;

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

// 前向声明
String findLastMessageByDevice(String deviceId);

// BLE特征值回调 (解析JSON指令)
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("📥 收到蓝牙指令：");
    Serial.println(rxValue);

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, rxValue);

    if (!error) {
      // 原有逻辑：数据同步开关
      if (doc.containsKey("syncing")) {
        needSync = doc["syncing"].as<bool>();
        Serial.println(needSync ? "✅ 同步已开启" : "⏹️ 同步已关闭");
      }

      // 接收对时指令：同步本地 + LoRa转发 + 查找指定设备最新消息
      if (doc.containsKey("cmd")) {
        String cmd = doc["cmd"].as<String>();
        if (cmd == "synctime" && doc.containsKey("time")) {
          String timeStr = doc["time"].as<String>();
          Serial.print("⏰ 收到对时发送指令: ");
          Serial.println(timeStr);

          // 1. 同步本地时间
          setTimeFromLora(timeStr);

          // 2. 如果携带 deviceId，在队列中查找该设备的最后一条消息
          if (doc.containsKey("deviceId")) {
            String targetId = doc["deviceId"].as<String>();
            String lastMsg = findLastMessageByDevice(targetId);
            if (lastMsg.length() > 0) {
              Serial.print("📋 ");
              Serial.print(targetId);
              Serial.print(" 最后一条消息: ");
              Serial.println(lastMsg);
            } else {
              Serial.print("⚠️ 队列中没有 ");
              Serial.println(targetId);
            }
          }

          // 3. 构造LoRa对时消息并转发
          String msg = String(MSG_TYPE_TIME) + "|" + deviceName + "|" + timeStr;
          int len = snprintf(timeSyncSendBuf, BUFFER_SIZE, "%s", msg.c_str());
          if (len < 0 || len >= BUFFER_SIZE) {
            timeSyncSendBuf[BUFFER_SIZE - 1] = '\0';
          }
          needSendTimeSync = true;
          Serial.print("📤 准备发送对时消息: ");
          Serial.println(timeSyncSendBuf);
        }
      }
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

// 从队列末尾向前查找指定设备的最后一条消息
String findLastMessageByDevice(String deviceId) {
  for (int i = dataCount - 1; i >= 0; i--) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, dataArray[i]) == DeserializationError::Ok) {
      const char* info = doc["info"];
      if (info != nullptr) {
        // info格式: "1|v4-6|xxx|xxx"，第二段是设备名
        String infoStr = String(info);
        int p1 = infoStr.indexOf('|');
        if (p1 > 0) {
          int p2 = infoStr.indexOf('|', p1 + 1);
          String msgDeviceId = (p2 > 0) ? infoStr.substring(p1 + 1, p2) : infoStr.substring(p1 + 1);
          if (msgDeviceId == deviceId) {
            return dataArray[i];
          }
        }
      }
    }
  }
  return "";
}

// 处理固件更新指令
void handleFirmwareUpdate(String loraData) {
  Serial.println("📦 开始处理固件更新...");
}

// 初始化BLE服务
void initBLE() {
  static MyServerCallbacks serverCallbacks;
  static MyCallbacks charCallbacks;
  BLECallbacks bleCallbacks = initBLEFun(deviceName, &serverCallbacks, &charCallbacks);
  pServer = bleCallbacks.pServer;
  pCharacteristic = bleCallbacks.pCharacteristic;
}




// LoRa发送完成回调
void OnTxDone(void) {
  Serial.println("✅ 对时发送完成，回到接收模式");
  needSendTimeSync = false;
  displayBuf[1] = "TX Done";
  Radio.Rx(0);
}

// LoRa发送超时回调
void OnTxTimeout(void) {
  Serial.println("❌ 对时发送超时，回到接收模式");
  needSendTimeSync = false;
  displayBuf[1] = "TX Timeout";
  Radio.Rx(0);
}

// LoRa接收超时回调
void OnRxTimeout(void) {
  Serial.println("⚠️ Radio接收超时!");
  // 超时后重新开启接收
  Radio.Rx(0);
   StaticJsonDocument<256> doc;
  doc["rssi"] = 0;
  doc["snr"] = 0;
  doc["info"] = "99|xxx|❌❌❌OnRxTimeout";
  doc["upDateDevice"] = deviceName;
  if (hasValidTime()) {
    // 有有效时间，存本地时间
    doc["time"] = getCurrentTime();
  } else {
    // 无有效时间，存millis()用于后续反算
    doc["ms"] = millis();
  }
}

// LoRa接收错误回调
void OnRxError(void) {
  Serial.println("❌ Radio接收错误!");
  // 错误后重新开启接收
  Radio.Rx(0);

  StaticJsonDocument<256> doc;
  doc["rssi"] = 0;
  doc["snr"] = 0;
  doc["info"] = "99|xxx|❌❌❌OnRxError";
  doc["upDateDevice"] = deviceName;
  if (hasValidTime()) {
    // 有有效时间，存本地时间
    doc["time"] = getCurrentTime();
  } else {
    // 无有效时间，存millis()用于后续反算
    doc["ms"] = millis();
  }
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


// 强制重置Radio(最强力恢复)
void forceResetRadio() {
  Serial.println("🔄 开始强制重置Radio...");

  // 步骤1: 进入睡眠模式
  Radio.Sleep();
  delay(20);
  Serial.println("  ✅ 已进入睡眠模式");

  initRadio();
  Serial.println("  ✅ Radio已重新初始化");
  delay(10);

  // 步骤4: 验证状态
  RadioState_t state = Radio.GetStatus();
  if (state == RF_RX_RUNNING) {
    Serial.println("✅ Radio强制重置成功,已进入接收状态");
  } else {
    Serial.print("❌ Radio重置失败,当前状态: ");
    Serial.println(state);
  }
}

// 初始化LoRa模块
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
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

  initRadio();
  initBLE();

  Serial.println("✅ 系统启动完成");
}
// 主循环
unsigned long lastDisplayUpdate = 0;
unsigned long lastrecdLoraTm = 0;

void loop() {
  unsigned long startm = millis();
  Radio.IrqProcess();

  // 处理BLE触发的对时LoRa发送
  if (needSendTimeSync) {
    Serial.print("📡 发送对时LoRa消息: ");
    Serial.println(timeSyncSendBuf);
    displayBuf[1] = "TX TimeSync";
    Radio.Send((uint8_t*)timeSyncSendBuf, strlen(timeSyncSendBuf));
    needSendTimeSync = false;
  }

  // 处理LoRa接收数据
  if (loraReceivedFlag) {
    loraReceivedFlag = false;
    Serial.print("  --> ");
    Serial.print("Rssi");
    Serial.print(lastRssi);
    Serial.print("  Snr");
    Serial.println(lastSnr);


    displayBuf[2] = "Rssi" + String(lastRssi) + " Snr" + String(lastSnr);
    Serial.print("Received LoRa: ");
    Serial.println(loraStr);

    // 解析LoRa消息类型
    int firstPipeIndex = String(loraStr).indexOf('|');
    if (firstPipeIndex > 0) {
      String typeStr = String(loraStr).substring(0, firstPipeIndex);
      int messageType = typeStr.toInt();

      Serial.print("📋 消息类型: ");
      Serial.println(messageType);

      // 类型2: 对时信息
      if (messageType == MSG_TYPE_TIME) {
        // 格式: 2|v4-1|2026/6/17 00:12:20
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          String timeStr = String(loraStr).substring(secondPipeIndex + 1);
          Serial.print("⏰ 收到对时信息: ");
          Serial.println(timeStr);
          setTimeFromLora(timeStr);
        }
      }
      // 类型3: 电量信息
      else if (messageType == MSG_TYPE_BATTERY) {
        // 格式: 3|设备名|电量值(如0.5、0.1)
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          String batteryStr = String(loraStr).substring(secondPipeIndex + 1);
          float batteryLevel = batteryStr.toFloat();
          Serial.print("🔋 收到电量信息: ");
          Serial.println(batteryLevel, 2);
          displayBuf[3] = "Bat:" + String(batteryLevel, 2) + "V";
        }
      }
      // 类型1: 定位信息（原有逻辑）
      else if (messageType == MSG_TYPE_GPS) {
        displayBuf[3] = String(loraStr).substring(0, 15);
      }
      // 类型10: 固件更新指令
      else if (messageType == MSG_TYPE_FIRMWARE) {
        Serial.println("🔄 收到固件更新指令");
        // TODO: 在此添加固件更新逻辑
        handleFirmwareUpdate(loraStr);
      }
      // 其他类型，直接显示
      else {
        displayBuf[3] = String(loraStr).substring(0, 15);
      }
    } else {
      // 没有分隔符，按原样显示
      displayBuf[3] = String(loraStr).substring(0, 15);
    }

    if (lastPayloadSize > 0) {

      StaticJsonDocument<256> doc;
      doc["rssi"] = (int)lastRssi;
      doc["snr"] = (int)lastSnr;
      doc["info"] = loraStr;
      doc["upDateDevice"] = deviceName;

      if (hasValidTime()) {
        // 有有效时间，存本地时间
        doc["time"] = getCurrentTime();
      } else {
        // 无有效时间，存millis()用于后续反算
        doc["ms"] = millis();
      }

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
    openLedByNum(1, 50);
    // Serial.print("测试收到一条消息用时");
    // Serial.println(millis() - startm);
    lastrecdLoraTm = startm;
  }

  // 超时检测与Radio状态恢复
  unsigned long timeSinceLastRecv = startm - lastrecdLoraTm;
  if (timeSinceLastRecv > SEND_INTERVAL_MS) {
    Serial.print("⚠️ 超过一个周期没有收到到数据: ");
    Serial.println(timeSinceLastRecv);

    // 检查Radio当前状态
    RadioState_t radioState = Radio.GetStatus();
    Serial.print("📻 Radio当前状态: ");
    Serial.println(radioState);

    // 无论状态如何,都执行强制重置(因为可能在接收状态但实际已死锁)
    Serial.println("🔧 检测到Radio可能死锁,执行强制重置...");
    forceResetRadio();

    lastrecdLoraTm = startm;
  }


  // 更新显示计数
  displayBuf[0] = "id:" + deviceName + " rec" + String(receiveCount);

  // BLE数据发送
  if (deviceConnected && needSync && dataCount > 0) {
    delay(200);  //延时200毫秒才发送蓝牙消息
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
  // Radio.Rx(0);  // 重新开启接收

  // Serial.println(getCurrentTime());
 
}