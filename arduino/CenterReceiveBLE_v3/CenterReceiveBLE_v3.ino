/*
 * HelTec ESP32 LoRa 接收中心 (牛羊GPS定位)
 * 功能：LoRa接收 + GPS采集 + BLE指令触发数据同步/对时转发
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

// ========================= BLE全局对象 =========================
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool needSync = false;

static RadioEvents_t RadioEvents;

// ========================= 数据缓存 =========================
// 显示缓冲区（4行OLED）
String displayBuf[4] = { "", "", "", "" };

// 数据队列（BLE同步用，满时覆盖最旧）
#define DATA_MAX_COUNT 99
String dataArray[DATA_MAX_COUNT];
int dataCount = 0;
int receiveCount = 0;

// 设备最后消息缓存（每设备仅保留最新一条，不随队列清空）
#define DEVICE_CACHE_MAX 50
String deviceCacheId[DEVICE_CACHE_MAX];
String deviceCacheMsg[DEVICE_CACHE_MAX];
int deviceCacheCount = 0;

String deviceName = "x-x";

// ========================= LoRa全局变量 =========================
char loraStr[BUFFER_SIZE];
char timeSyncSendBuf[BUFFER_SIZE];
bool needSendTimeSync = false;
unsigned long scheduledSendMs = 0;  // 定时发送时间点(millis)

bool needPlaLed = false;
bool loraReceivedFlag = false;
volatile int16_t lastRssi = 0;
volatile int8_t lastSnr = 0;
volatile uint16_t lastPayloadSize = 0;

// ========================= 前向声明 =========================
String findLastMessageByDevice(String deviceId);
unsigned long calcRxWindowStartMs(String msgJson);

StaticJsonDocument<200> docCom;  // BLE指令解析用（全局复用）

// ========================= BLE回调 =========================

// BLE服务器连接/断开回调
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
void meshCmdInfomsg(String rxValue) {
  //    {"cmd":"setfreq","value":5,"deviceId":"v4-4"}
  //      {"deviceId":"v4-4","cmd":"setfreq","value":5}
  Serial.println(rxValue);
  DeserializationError error = deserializeJson(docCom, rxValue);
  if (error) {
    return;
  }

  // 数据同步开关
  if (docCom.containsKey("syncing")) {
    needSync = docCom["syncing"].as<bool>();
    Serial.println(needSync ? "✅ 同步已开启" : "⏹️ 同步已关闭");
    return;
  }

  // 指令处理
  if (!docCom.containsKey("cmd")) {
    return;
  }
  String cmd = docCom["cmd"].as<String>();

  // 获取设备列表
  if (cmd == "getDeviceList") {
    Serial.println("获取设备列表");
    StaticJsonDocument<256> doc;
    doc["cmd"] = "getDeviceList";
    JsonArray infoArray = doc.createNestedArray("info");
    for (int i = 0; i < deviceCacheCount; i++) {
      infoArray.add(deviceCacheId[i]);
    }
    String str;
    serializeJson(doc, str);
    Serial.println(str);
    pCharacteristic->setValue(str.c_str());
    pCharacteristic->notify();
    return;
  }

  // 对时指令：同步本地时间
  if (cmd == "synctime" && docCom.containsKey("time")) {
    String timeStr = docCom["time"].as<String>();
    Serial.print("⏰ 收到对时发送指令: ");
    Serial.println(timeStr);
    setTimeFromLora(timeStr);
  }

  // 携带deviceId时，计算接收窗口并定时发送对时
  if (docCom.containsKey("deviceId")) {
    String targetId = docCom["deviceId"].as<String>();
    String lastMsg = findLastMessageByDevice(targetId);
    if (lastMsg.length() > 0) {
      Serial.print("📋 ");
      Serial.print(targetId);
      Serial.print(" 最后一条消息: ");
      Serial.println(lastMsg);
      unsigned long rxStartMs = calcRxWindowStartMs(lastMsg);
      if (rxStartMs > 0) {
        unsigned long nowMs = millis();
        if (rxStartMs > nowMs) {
          scheduledSendMs = rxStartMs;
          needSendTimeSync = true;
          unsigned long waitSec = (rxStartMs - nowMs) / 1000;
          Serial.printf("⏳ 将在 %lu 秒后（接收窗口开启时）发送对时\n", waitSec);
        }
      }
    } else {
      String str = "{\"cmd\":\"tip\", \"info\":\"⚠️ 队列中没有" + targetId + "的记录\"}";
      Serial.println(str);
      pCharacteristic->setValue(str.c_str());
      pCharacteristic->notify();
    }
  }
}

// BLE特征值写入回调（解析JSON指令）
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("📥 收到蓝牙指令：");
    meshCmdInfomsg(rxValue);
  }
};

// ========================= 数据队列操作 =========================

// 添加数据到队列（满时覆盖最旧）
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

// 取出并删除队列头部（FIFO）
String getAndRemoveFirstData() {
  if (dataCount == 0) {
    return "";
  }
  String first = dataArray[0];
  for (int i = 0; i < dataCount - 1; i++) {
    dataArray[i] = dataArray[i + 1];
  }
  dataArray[--dataCount] = "";
  return first;
}

// ========================= 设备缓存操作 =========================

// 从info字段提取设备ID（第二段，如"v3-8"）
String extractDeviceIdFromInfo(String infoStr) {
  int p1 = infoStr.indexOf('|');
  if (p1 <= 0) {
    return "";
  }
  int p2 = infoStr.indexOf('|', p1 + 1);
  return (p2 > 0) ? infoStr.substring(p1 + 1, p2) : infoStr.substring(p1 + 1);
}

// 更新设备最后消息缓存（已存在则覆盖，否则追加）
void updateDeviceCache(String deviceId, String msgJson) {
  if (deviceId.length() == 0) {
    return;
  }
  for (int i = 0; i < deviceCacheCount; i++) {
    if (deviceCacheId[i] == deviceId) {
      deviceCacheMsg[i] = msgJson;
      return;
    }
  }
  if (deviceCacheCount < DEVICE_CACHE_MAX) {
    deviceCacheId[deviceCacheCount] = deviceId;
    deviceCacheMsg[deviceCacheCount] = msgJson;
    deviceCacheCount++;
  }
}

// 查找指定设备的最后一条消息
String findLastMessageByDevice(String deviceId) {
  for (int i = 0; i < deviceCacheCount; i++) {
    if (deviceCacheId[i] == deviceId) {
      return deviceCacheMsg[i];
    }
  }
  return "";
}

// ========================= 接收窗口计算 =========================

// 根据设备时隙计算接收窗口起始时间(millis)
// 原理：设备在slotTime发送，RX窗口在周期末尾rxWinSec秒
unsigned long calcRxWindowStartMs(String msgJson) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msgJson) != DeserializationError::Ok) {
    return 0;
  }

  // 从info提取设备名，解析设备索引（info格式: "2|v3-8|2000/1/1 08:14:14|15"）
  const char *infoCstr = doc["info"];
  if (infoCstr == nullptr) {
    return 0;
  }
  String infoStr = String(infoCstr);
  int p1 = infoStr.indexOf('|');
  if (p1 < 0) {
    return 0;
  }
  int p2 = infoStr.indexOf('|', p1 + 1);
  if (p2 < 0) {
    return 0;
  }
  String devName = infoStr.substring(p1 + 1, p2);
  int dashPos = devName.lastIndexOf('-');
  if (dashPos < 0) {
    return 0;
  }
  int deviceIdx = devName.substring(dashPos + 1).toInt();

  int totalDevices = getTotalDevices();
  const unsigned long intervalSec = SEND_INTERVAL_MS / 1000;
  const unsigned long rxWinSec = 5;

  // 设备时隙 = deviceIdx * (周期 / 总设备数)
  float slotDuration = (float)intervalSec / totalDevices;
  float slotTime = deviceIdx * slotDuration;

  // 从收到消息到设备RX窗口中心的延迟
  unsigned long delaySec = (unsigned long)(intervalSec - slotTime - rxWinSec / 1.50);

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
    waitSec += intervalSec;  // 窗口已过，推迟到下一周期
    Serial.println("⚠️ 窗口已过，推迟到下一周期");
  }

  Serial.printf("📌 %s idx=%d, 时隙%.2f秒, delay=%lu秒, 已过%lu秒, 等待%ld秒\n",
                devName.c_str(), deviceIdx, slotTime, delaySec, elapsedSec, waitSec);
  return millis() + (unsigned long)waitSec * 1000UL;
}

// ========================= 初始化 =========================

// 初始化BLE服务
void initBLE() {
  static MyServerCallbacks serverCallbacks;
  static MyCallbacks charCallbacks;
  BLECallbacks bleCallbacks = initBLEFun(deviceName, &serverCallbacks, &charCallbacks);
  pServer = bleCallbacks.pServer;
  pCharacteristic = bleCallbacks.pCharacteristic;
}

// 初始化LoRa Radio
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  initPanRadio(&RadioEvents);
  Radio.Rx(0);
}

// 强制重置Radio（睡眠+重新初始化）
void forceResetRadio() {
  Serial.println("🔄 开始强制重置Radio...");
  Radio.Sleep();
  delay(20);
  initRadio();
  Serial.println("  ✅ Radio已重新初始化");
  delay(10);

  RadioState_t state = Radio.GetStatus();
  if (state == RF_RX_RUNNING) {
    Serial.println("✅ Radio强制重置成功,已进入接收状态");
  } else {
    Serial.print("❌ Radio重置失败,当前状态: ");
    Serial.println(state);
  }
}

// ========================= LoRa回调 =========================

void OnTxDone(void) {
  Serial.println("✅ 对时发送完成，回到接收模式");
  needSendTimeSync = false;
  displayBuf[1] = "TX Done";
  Radio.Rx(0);
}

void OnTxTimeout(void) {
  Serial.println("❌ 对时发送超时，回到接收模式");
  needSendTimeSync = false;
  displayBuf[1] = "TX Timeout";
  Radio.Rx(0);
}

void OnRxTimeout(void) {
  Serial.println("⚠️ Radio接收超时!");
  Radio.Rx(0);
}

void OnRxError(void) {
  Serial.println("❌ Radio接收错误!");
  Radio.Rx(0);
}

// LoRa接收回调（仅拷贝数据，耗时操作在主循环处理）
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

// ========================= DTU上报 =========================

// 通过串口2将LoRa数据上报给DTU
void sendLoraInfoUseDtu(String str, String rssi, String snr) {
  String json = "{"
                "\"id\":"
                + String(millis()) + ","
                                     "\"version\":\"1.0\","
                                     "\"method\":\"thing.event.property.post\","
                                     "\"params\":{"
                                     "\"lorainfo\":\""
                + str + "\","
                        "\"upDateDevice\":\""
                + deviceName + "\","
                               "\"rssi\":"
                + rssi + ","
                         "\"snr\":"
                + snr + "}"
                        "}";
  Serial.println("上报报文：" + json);
  Serial2.println(json);
}

// cominfo缓存数组（最多存10条）
#define COMINFO_MAX 10
String cominfoArray[COMINFO_MAX];
int cominfoCount = 0;

/**
 * 接收 Serial2 (DTU) 数据，拆分多个拼接JSON并提取cominfo
 */
void receiveDtuData() {
  if (Serial2.available() <= 0) {
    return;
  }

  // 1. 读取本次全部数据
  String raw = "";
  while (Serial2.available() > 0) {
    raw += (char)Serial2.read();
    delay(2);  // 等待下一个字节
  }
  Serial2.flush();

  // Serial.print("原始数据: ");
  // Serial.println(raw);

  // 2. 用大括号计数法拆分多个拼接的JSON对象
  cominfoCount = 0;
  int depth = 0;
  int start = -1;

  for (int i = 0; i < raw.length(); i++) {
    char c = raw[i];
    if (c == '{') {
      if (depth == 0) {
        start = i;  // 记录JSON起始位置
      }
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0 && start >= 0) {
        // 提取一个完整JSON对象
        String jsonStr = raw.substring(start, i + 1);
        // Serial.print("拆分JSON: ");
        // Serial.println(jsonStr);

        // 3. 解析并提取cominfo
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) {
          Serial.print("解析失败: ");
          Serial.println(err.c_str());
        } else {
          const char *cominfo = doc["params"]["cominfo"];
          if (cominfo != nullptr) {
            Serial.print("cominfo: ");
            Serial.println(cominfo);
            // 存入数组
            if (cominfoCount < COMINFO_MAX) {
              cominfoArray[cominfoCount] = String(cominfo);
              cominfoCount++;
            }
          } else {
            // Serial.println("未找到cominfo字段");
          }
        }
        start = -1;
      }
    }
  }
  if (cominfoCount > 0) {
    // 4. 打印汇总结果
    Serial.println("---- cominfo汇总 ----");
    for (int i = 0; i < cominfoCount; i++) {
      Serial.print("[");
      Serial.print(i);
      Serial.print("] ");
      // Serial.println(cominfoArray[i]);

      meshCmdInfomsg(cominfoArray[i]);
    }
    Serial.print("共 ");
    Serial.print(cominfoCount);
    Serial.println(" 条");
    Serial.println("--------------------");
  }
}
// ========================= 系统初始化 =========================

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);

  Serial2.begin(115200, SERIAL_8N1, 17, 18);
  // Serial2.begin(115200, SERIAL_8N1, 45, 46);
  delay(1000);

  deviceName = makeDivceName();
  displayBuf[0] = "id:" + deviceName + " rec";

  initRadio();
  initBLE();

  Serial.println("✅ 系统启动完成");
}

// ========================= 主循环 =========================
unsigned long lastDisplayUpdate = 0;
unsigned long lastrecdLoraTm = 0;
unsigned long lastUpSelfTm = 0;
void loop() {
 
  Radio.IrqProcess();
  receiveDtuData();

  if (lastUpSelfTm < (millis() - SEND_INTERVAL_MS)) {
    lastUpSelfTm = millis();
    sendLoraInfoUseDtu(String(MSG_TYPE_BATTERY) + "|" + deviceName + "|1.00|1119|948|5.08|285", "0", "0");
  }

  // 处理BLE触发的定时LoRa发送（等待接收窗口到达）
  if (needSendTimeSync) {
    unsigned long nowMs = millis();
    if (scheduledSendMs > 0 && nowMs < scheduledSendMs) {
      // 窗口未到，每秒打印倒计时
      static unsigned long lastPrint = 0;
      if (nowMs - lastPrint >= 1000) {
        lastPrint = nowMs;
        Serial.printf("⏳ 等待接收窗口... 还剩 %lu 秒\n", (scheduledSendMs - nowMs) / 1000);
      }
    } else {
      // 窗口已到达，构建并发送消息
      if (docCom.containsKey("cmd")) {
        String msg;
        String cmd = docCom["cmd"].as<String>();
        if (cmd == "synctime" && docCom.containsKey("time")) {
          String currentTimeStr = getCurrentTime();
          msg = String(MSG_TYPE_TIME) + "|" + deviceName + "|" + currentTimeStr;
          Serial.print("📡 发送对时LoRa消息: ");
        } else if (cmd == "setfreq") {
          String targetId = docCom["deviceId"].as<String>();
          msg = String(MSG_TYPE_COM) + "|" + targetId;
          if (docCom.containsKey("value")) {
            msg += "|" + docCom["value"].as<String>();
          } else {
            msg += "|value=null";
          }
          Serial.print("📡 下达指令: ");
        } else {
          msg = "no cmd:" + cmd;
          Serial.print("📡 没有对应的cmd: ");
          Serial.println(cmd);
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
    Serial.printf("  --> Rssi%d  Snr%d\n", lastRssi, lastSnr);
    displayBuf[2] = "Rssi" + String(lastRssi) + " Snr" + String(lastSnr);
    Serial.print("Received LoRa: ");
    Serial.println(loraStr);

    // 解析消息类型并处理
    int firstPipeIndex = String(loraStr).indexOf('|');
    if (firstPipeIndex > 0) {
      int messageType = String(loraStr).substring(0, firstPipeIndex).toInt();
      // Serial.print("📋 消息类型: ");
      // Serial.println(messageType);

      if (messageType == MSG_TYPE_TIME) {
        // 对时信息：2|设备名|时间字符串
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          String timeStr = String(loraStr).substring(secondPipeIndex + 1);
          // Serial.print("⏰ 收到对时信息: ");
          // Serial.println(timeStr);
          setTimeFromLora(timeStr);
        }
      } else if (messageType == MSG_TYPE_BATTERY) {
        // 电量信息：3|设备名|电量值
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          float batteryLevel = String(loraStr).substring(secondPipeIndex + 1).toFloat();
          // Serial.print("🔋 收到电量信息: ");
          // Serial.println(batteryLevel, 2);
          displayBuf[3] = "Bat:" + String(batteryLevel, 2) + "V";
        }
      } else if (messageType == MSG_TYPE_GPS) {
        // GPS定位信息
        displayBuf[3] = String(loraStr).substring(0, 15);
      } else if (messageType == MSG_TYPE_FIRMWARE) {
        // 固件更新指令（待实现）
        Serial.println("🔄 收到固件更新指令（未处理）");
      } else {
        // 其他类型，直接显示
        displayBuf[3] = String(loraStr).substring(0, 15);
      }

      sendLoraInfoUseDtu(String(loraStr), String(lastRssi), String(lastSnr));
    } else {
      displayBuf[3] = String(loraStr).substring(0, 15);
    }

    // 存入队列并更新设备缓存
    if (lastPayloadSize > 0) {
      StaticJsonDocument<256> doc;
      doc["rssi"] = (int)lastRssi;
      doc["snr"] = (int)lastSnr;
      doc["info"] = loraStr;
      doc["upDateDevice"] = deviceName;
      if (hasValidTime()) {
        doc["time"] = getCurrentTime();
      } else {
        doc["ms"] = millis();
      }

      String jsonData;
      serializeJson(doc, jsonData);
      addDataToQueue(jsonData);

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

  // LED闪烁提示
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(1, 50);
    lastrecdLoraTm =  millis();
  }

  // 超时检测：超过2个周期未收到数据则强制重置Radio
  unsigned long timeSinceLastRecv =  millis() - lastrecdLoraTm;
  if (timeSinceLastRecv > SEND_INTERVAL_MS * 2) {
    Serial.print("⚠️ 超过2个周期没有收到数据: ");
    Serial.println(timeSinceLastRecv);
    RadioState_t radioState = Radio.GetStatus();
    Serial.print("📻 Radio当前状态: ");
    Serial.println(radioState);
    Serial.println("🔧 检测到Radio可能死锁,执行强制重置...");
    forceResetRadio();
    lastrecdLoraTm =  millis();
  }

  // 更新显示
  displayBuf[0] = "id:" + deviceName + " rec" + String(receiveCount);

  // BLE数据同步发送
  if (deviceConnected && needSync && dataCount > 0) {
    delay(50);
    String data = getAndRemoveFirstData();
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();
    Serial.print("✅ 同步发送：");
    Serial.println(data);
    Serial.print("📊 剩余：");
    Serial.println(dataCount);
  }

  // OLED刷新（每500ms一次）
  unsigned long now = millis();
  if (now - lastDisplayUpdate >= 500) {
    showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);
    lastDisplayUpdate = now;
  }

  delay(100);
}
