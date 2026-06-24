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

// 设备最后消息缓存（每个设备只保留最新一条，不随dataArray同步清空）
#define DEVICE_CACHE_MAX 50
String deviceCacheId[DEVICE_CACHE_MAX];   // 设备ID
String deviceCacheMsg[DEVICE_CACHE_MAX];  // 该设备最后一条完整JSON
int deviceCacheCount = 0;

String deviceName = "x-x";


// LoRa接收缓冲区
char loraStr[BUFFER_SIZE];

// LoRa对时发送缓冲区
char timeSyncSendBuf[BUFFER_SIZE];
bool needSendTimeSync = false;
unsigned long scheduledSendMs = 0;  // 窗口发送时间点(millis)

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
unsigned long calcRxWindowStartMs(String msgJson);

StaticJsonDocument<200> docCom;

// BLE特征值回调 (解析JSON指令)
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("📥 收到蓝牙指令：");
    Serial.println(rxValue);
    DeserializationError error = deserializeJson(docCom, rxValue);

    if (!error) {
      // 原有逻辑：数据同步开关
      if (docCom.containsKey("syncing")) {
        needSync = docCom["syncing"].as<bool>();
        Serial.println(needSync ? "✅ 同步已开启" : "⏹️ 同步已关闭");
      }
      // 接收对时指令：同步本地 + LoRa转发 + 查找指定设备最新消息
      if (docCom.containsKey("cmd")) {
        String cmd = docCom["cmd"].as<String>();

        // 2. 如果携带 deviceId，查找该设备的最后一条消息并计算接收窗口
        if (docCom.containsKey("deviceId")) {
          String targetId = docCom["deviceId"].as<String>();
          String lastMsg = findLastMessageByDevice(targetId);
          if (lastMsg.length() > 0) {
            Serial.print("📋 ");
            Serial.print(targetId);
            Serial.print(" 最后一条消息: ");
            Serial.println(lastMsg);

            // 计算接收窗口起始时间
            unsigned long rxStartMs = calcRxWindowStartMs(lastMsg);
            if (rxStartMs > 0) {
              unsigned long nowMs = millis();
              if (rxStartMs > nowMs) {
                // 窗口在未来，定时发送
                scheduledSendMs = rxStartMs;
                needSendTimeSync = true;
                unsigned long waitSec = (rxStartMs - nowMs) / 1000;
                Serial.printf("⏳ 将在 %lu 秒后（接收窗口开启时）发送对时\n", waitSec);
              }
            }
          } else {


            // String str="⚠️ 队列中没有 "+targetId;
            String str = "{\"cmd\":\"tip\", \"info\":\"⚠️ 队列中没有"+targetId+"的记录\"}";
            Serial.println(str);
            pCharacteristic->setValue(str.c_str());
            pCharacteristic->notify();
          }


          if (cmd == "synctime" && docCom.containsKey("time")) {
            String timeStr = docCom["time"].as<String>();
            Serial.print("⏰ 收到对时发送指令: ");
            Serial.println(timeStr);

            // 1. 同步本地时间
            setTimeFromLora(timeStr);
          }
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

// 从info字段提取设备ID（第二段，如"v3-8"）
String extractDeviceIdFromInfo(String infoStr) {
  int p1 = infoStr.indexOf('|');
  if (p1 <= 0) return "";
  int p2 = infoStr.indexOf('|', p1 + 1);
  return (p2 > 0) ? infoStr.substring(p1 + 1, p2) : infoStr.substring(p1 + 1);
}

// 更新设备最后消息缓存
void updateDeviceCache(String deviceId, String msgJson) {
  if (deviceId.length() == 0) return;
  // 查找是否已存在该设备
  for (int i = 0; i < deviceCacheCount; i++) {
    if (deviceCacheId[i] == deviceId) {
      deviceCacheMsg[i] = msgJson;
      return;
    }
  }
  // 新设备，追加
  if (deviceCacheCount < DEVICE_CACHE_MAX) {
    deviceCacheId[deviceCacheCount] = deviceId;
    deviceCacheMsg[deviceCacheCount] = msgJson;
    deviceCacheCount++;
  }
}

// 从设备缓存中查找指定设备的最后一条消息
String findLastMessageByDevice(String deviceId) {
  for (int i = 0; i < deviceCacheCount; i++) {
    if (deviceCacheId[i] == deviceId) {
      return deviceCacheMsg[i];
    }
  }
  return "";
}

// 根据设备ID和中心接收时间计算设备的接收窗口
// 原理：设备在时隙slotTime发送，RX窗口在时隙后(interval - slotTime - rxWinSec)秒
// 例: v3-8 slot=14.55s, delay=60-14.55-5=40.45s
unsigned long calcRxWindowStartMs(String msgJson) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msgJson) != DeserializationError::Ok) return 0;

  // 从 info 提取设备名，解析设备索引
  // info格式: "2|v3-8|2000/1/1 08:14:14|15"
  const char *infoCstr = doc["info"];
  if (infoCstr == nullptr) return 0;
  String infoStr = String(infoCstr);
  int p1 = infoStr.indexOf('|');
  if (p1 < 0) return 0;
  int p2 = infoStr.indexOf('|', p1 + 1);
  if (p2 < 0) return 0;
  String devName = infoStr.substring(p1 + 1, p2);
  int dashPos = devName.lastIndexOf('-');
  if (dashPos < 0) return 0;
  int deviceIdx = devName.substring(dashPos + 1).toInt();

  int totalDevices = getTotalDevices();                       // 33
  const unsigned long intervalSec = SEND_INTERVAL_MS / 1000;  // 60
  const unsigned long rxWinSec = 5;

  // 设备时隙 = deviceIdx * (周期 / 总设备数)
  float slotDuration = (float)intervalSec / totalDevices;
  float slotTime = deviceIdx * slotDuration;  // v3-8: 8*1.818=14.55

  // 从收到消息到设备RX窗口中心的延迟
  // 设备在slotTime发送，RX窗口在周期最后rxWinSec秒，在窗口中心发送
  // delay = interval - slotTime - rxWinSec/2
  unsigned long delaySec = (unsigned long)(intervalSec - slotTime - rxWinSec / 2.0);  // 42秒

  // 扣除BLE指令到达前已流逝的时间
  unsigned long elapsedSec = 0;
  const char *recvTimeCstr = doc["time"];
  if (recvTimeCstr != nullptr) {
    int rh, rm, rs;
    if (sscanf(recvTimeCstr, "%*d/%*d/%*d %d:%d:%d", &rh, &rm, &rs) == 3) {
      unsigned long recvTimeOfDay = (unsigned long)rh * 3600UL + (unsigned long)rm * 60UL + rs;
      String nowStr = getCurrentTime();
      int nh, nm, ns;
      if (sscanf(nowStr.c_str(), "%*d/%*d/%*d %d:%d:%d", &nh, &nm, &ns) == 3) {
        unsigned long nowTimeOfDay = (unsigned long)nh * 3600UL + (unsigned long)nm * 60UL + ns;
        elapsedSec = (nowTimeOfDay >= recvTimeOfDay) ? (nowTimeOfDay - recvTimeOfDay) : 0;
      }
    }
  }
  long waitSec = (long)delaySec - (long)elapsedSec;
  if (waitSec < 0) {
    waitSec += intervalSec;  // 窗口已过，放到下一个周期
    Serial.println("⚠️ 窗口已过，推迟到下一周期");
  }

  Serial.printf("📌 %s idx=%d, 时隙%.2f秒, delay=%lu秒, 已过%lu秒, 等待%ld秒\n",
                devName.c_str(), deviceIdx, slotTime, delaySec, elapsedSec, waitSec);

  return millis() + (unsigned long)waitSec * 1000UL;
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

  // 处理BLE触发的对时LoRa发送（等待接收窗口）
  if (needSendTimeSync) {
    unsigned long nowMs = millis();
    if (scheduledSendMs > 0 && nowMs < scheduledSendMs) {
      // 窗口未到，等待（每500ms打印一次倒计时）
      static unsigned long lastPrint = 0;
      if (nowMs - lastPrint >= 1000) {
        lastPrint = nowMs;
        Serial.printf("⏳ 等待接收窗口... 还剩 %lu 秒\n", (scheduledSendMs - nowMs) / 1000);
      }
    } else {
      // 窗口已到达（或无窗口延迟），用当前实时时间发送
      if (docCom.containsKey("cmd")) {
        String msg;
        String cmd = docCom["cmd"].as<String>();
        if (cmd == "synctime" && docCom.containsKey("time")) {
          String currentTimeStr = getCurrentTime();
          msg = String(MSG_TYPE_TIME) + "|" + deviceName + "|" + currentTimeStr;
          Serial.print("📡 发送对时LoRa消息: ");
        } else {
          String targetId = docCom["deviceId"].as<String>();
          msg = String(MSG_TYPE_COM) + "|" + targetId;
          if (docCom.containsKey("value")) {
            msg += "|" + docCom["value"].as<String>();
          } else {
            msg += "|value=null";
          }
          Serial.print("📡 下达指令: ");
        }
        snprintf(timeSyncSendBuf, BUFFER_SIZE, "%s", msg.c_str());
        timeSyncSendBuf[BUFFER_SIZE - 1] = '\0';

        Serial.println(timeSyncSendBuf);
        displayBuf[1] = "TX TimeSync";
        Radio.Send((uint8_t *)timeSyncSendBuf, strlen(timeSyncSendBuf));
      }
      needSendTimeSync = false;
      scheduledSendMs = 0;
    }
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

      // 同步更新设备最后消息缓存
      const char *infoRaw = doc["info"];
      if (infoRaw != nullptr) {
        String devId = extractDeviceIdFromInfo(String(infoRaw));
        updateDeviceCache(devId, jsonData);
      }

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
  if (timeSinceLastRecv > SEND_INTERVAL_MS * 2) {
    Serial.print("⚠️ 超过2个周期没有收到到数据: ");
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
    delay(50);  //延时200毫秒才发送蓝牙消息
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