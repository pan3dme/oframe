/*
 * HelTec ESP32 LoRa 接收中心 (牛羊GPS定位)
 * 功能：LoRa接收 + GPS采集 + BLE指令触发数据同步/对时转发
 */

#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <pan3dme.h>
#include <time.h>

// ========================= BLE全局对象 =========================
bool needSync = false;
BLECharacteristic *pCharacteristic = NULL;
static RadioEvents_t RadioEvents;

// ========================= 数据缓存 =========================
// 数据队列（BLE同步用，满时覆盖最旧）
#define DATA_MAX_COUNT 99
String dataArray[DATA_MAX_COUNT];
int dataCount = 0;

// 设备最后消息缓存（每设备仅保留最新一条，不随队列清空）
#define DEVICE_CACHE_MAX 50
String deviceCacheId[DEVICE_CACHE_MAX];
String deviceCacheMsg[DEVICE_CACHE_MAX];
int deviceCacheCount = 0;

int rtcSendCount = 0;
String batterystr = "";
String deviceName = "x-x";

// ========================= LoRa全局变量 =========================
char loraStr[BUFFER_SIZE];
char sendData[BUFFER_SIZE];  // 发送数据缓存

// 回调标记（主循环根据此标志处理数据）
bool loraReceivedFlag = false;
int16_t lastRssi = 0;
int8_t lastSnr = 0;

StaticJsonDocument<200> docCom;  // BLE指令解析用（全局复用）

// ========================= BLE回调 =========================

// BLE服务器连接/断开回调
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    Serial.println("✅ 小程序已连接");
  }
  void onDisconnect(BLEServer *pServer) {
    needSync = false;
    Serial.println("❌ 断开连接 | 同步已关闭");
    pServer->startAdvertising();
  }
};
#define TARGET_ID_MAX 20
String targetIdList[TARGET_ID_MAX];
int targetIdCount = 0;

// 检查 targetId 是否已存在
bool isTargetIdExist(String id) {
  for (int i = 0; i < targetIdCount; i++) {
    if (targetIdList[i] == id)
      return true;
  }
  return false;
}

// 添加 targetId（若已存在则忽略）
void addTargetId(String id) {
  if (id.length() == 0)
    return;
  if (isTargetIdExist(id)) {
    Serial.println("⚠️ targetId 已存在，忽略添加");
    return;
  }
  if (targetIdCount < TARGET_ID_MAX) {
    targetIdList[targetIdCount++] = id;
    Serial.print("✅ 已添加 targetId: ");
    Serial.println(id);
  } else {
    Serial.println("❌ targetId 列表已满！");
  }
}
void removeTargetByDeviceId(String deviceId) {
  for (int i = targetIdCount - 1; i >= 0; i--) {
    deserializeJson(docCom, targetIdList[i]);
    if (docCom.containsKey("cmd") && docCom.containsKey("deviceId")) {
      String targetId = docCom["deviceId"].as<String>();
      if (targetId == deviceId) {
        removeTargetId(targetIdList[i]);
      }
    }
  }
}
// 删除指定的 targetId
void removeTargetId(String id) {
  for (int i = 0; i < targetIdCount; i++) {
    if (targetIdList[i] == id) {
      // 将后面的元素前移
      for (int j = i; j < targetIdCount - 1; j++) {
        targetIdList[j] = targetIdList[j + 1];
      }
      targetIdCount--;
      Serial.print("✅ 已删除 targetId: ");
      Serial.println(id);
      return;
    }
  }
  Serial.print("⚠️ 未找到 targetId: ");
  Serial.println(id);
}

// 清空所有（可选）
void clearTargetIdList() {
  targetIdCount = 0;
  Serial.println("🗑️ 已清空所有 targetId");
}

void meshCmdInfomsg(String rxValue) {
  Serial.println(rxValue);
  DeserializationError error = deserializeJson(docCom, rxValue);
  if (error) {
    Serial.print(" error ");
    return;
  }
  if (docCom.containsKey("syncing")) {
    needSync = docCom["syncing"].as<bool>();
    Serial.println(needSync ? "✅ 同步已开启" : "⏹️ 同步已关闭");
    return;
  }
  if (docCom.containsKey("cmd") && docCom.containsKey("deviceId")) {
    int type = docCom["value"].as<int>();
    // 先移除原来对应设备的
    removeTargetByDeviceId(docCom["deviceId"].as<String>());
    addTargetId(rxValue);
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

// ========================= 初始化 =========================

// 初始化BLE服务
void initBLE() {
  static MyServerCallbacks serverCallbacks;
  static MyCallbacks charCallbacks;

  BLECallbacks bleCallbacks =
    initBLEFun(deviceName, &serverCallbacks, &charCallbacks);
  // pServer = bleCallbacks.pServer;
  pCharacteristic = bleCallbacks.pCharacteristic;
}

// 初始化LoRa Radio
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  initPanRadio(&RadioEvents, TX_POWER);
  Radio.Rx(0);
}

// ========================= LoRa回调 =========================
void OnTxDone(void) {
  // Serial.println("下发LORA完成，回到接收模式");
  Radio.Rx(0);
}
void OnTxTimeout(void) {
  Serial.println("❌ 发送超时，回到接收模式");
  Radio.Rx(0);
}
void OnRxTimeout(void) {
  // Serial.println("⚠️ Radio接收超时!");
  Radio.Rx(0);
}
void OnRxError(void) {
  Serial.println("❌ Radio接收错误!");
  Radio.Rx(0);
}
// LoRa接收回调（仅拷贝数据+设标记，耗时操作在主循环处理）
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE && size > 0) {
    memcpy(loraStr, payload, size);
    loraStr[size] = '\0';
    lastRssi = rssi;
    lastSnr = snr;
    loraReceivedFlag = true;
  } else {
    loraStr[0] = '\0';
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
  // Serial.println("上报报文：" + json);
  Serial2.println(json);
}
// 接收 Serial2 (DTU) 数据，拆分多个拼接JSON并提取cominfo
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

  // 2. 用大括号计数法拆分多个拼接的JSON对象
  String cominfoArray[10];
  int cominfoCount = 0;
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

        // 解析并提取cominfo
        DeserializationError err = deserializeJson(docCom, jsonStr);
        if (err) {
          Serial.print("解析失败: ");
          Serial.println(err.c_str());
        } else {
          const char *cominfo = docCom["params"]["cominfo"];
          if (cominfo != nullptr) {
            Serial.print("cominfo: ");
            Serial.println(cominfo);
            if (cominfoCount < 10) {
              cominfoArray[cominfoCount] = String(cominfo);
              cominfoCount++;
            }
          }
        }
        start = -1;
      }
    }
  }
  if (cominfoCount > 0) {
    Serial.println("---- cominfo汇总 ----");
    for (int i = 0; i < cominfoCount; i++) {
      Serial.print("[");
      Serial.print(i);
      Serial.print("] ");
      meshCmdInfomsg(cominfoArray[i]);
    }
    Serial.print("共 ");
    Serial.print(cominfoCount);
    Serial.println(" 条");
    Serial.println("--------------------");
  }
}

void sendLoraToDeviceid(String dataStr) {
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }
  // Serial.print(getCurrentTime(true));
  Serial.print("SEND DOWN：");
  Serial.print(sendData);
  Serial.print("len:");
  Serial.println(strlen(sendData));
  Radio.Send((uint8_t *)sendData, strlen(sendData));
}
String needSyncTimeDeviceid;
long down_syn_time = 0;
// ========================= 下发指令 =========================
void sendDownInfo(String loraStr) {
  String deviceId = extractDeviceIdFromInfo(loraStr);
  String dataStr = "";
  String selectCmdStr = "";
  for (int i = 0; i < targetIdCount; i++) {
    deserializeJson(docCom, targetIdList[i]);
    if (docCom.containsKey("deviceId")) {
      String targetId = docCom["deviceId"].as<String>();
      if (targetId == deviceId) {  // 只处理发给当前设备的指令
        selectCmdStr = targetIdList[i];
        break;  // 找到第一个即停止，或改为处理所有
      }
    }
  }

  if (selectCmdStr.length() > 0) {
    Serial.print("✅标记了下发数据");
    Serial.println(selectCmdStr);
    String cmd = docCom["cmd"].as<String>();
    if (cmd == "setfreq") {
      int type = docCom["value"].as<int>();
      int tm = docCom["tm"].as<int>();
      dataStr = String(type) + "|" + deviceId + "|" + tm;
    }
    if (cmd == "sendmode") {  //新板本下一个版本的下发指令
      int type = docCom["value"].as<int>();
      int tm = docCom["tm"].as<int>();
      dataStr = String(MSG_TYPE_COM) +"|" + deviceId + "|" + cmd + "|" + String(type) + "|" + String(tm);
    }
    removeTargetId(selectCmdStr);
    sendLoraToDeviceid(dataStr);
  } else {
    needSyncTimeDeviceid = deviceId;
    down_syn_time = millis() + 1500;
    // TODO: 此处 delay(2000)
    // 用于协调多中继，但会阻塞系统，后期改为非阻塞方式（millis）
    // delay(2000); // 如果普通对时就延时2秒，优先指令下载
    // dataStr = String(MSG_TYPE_SYN_TIME) + "|" + deviceId;
    // dataStr += "|" + getCurrentTime(true);
  }
}

// ========================= 主循环处理LoRa数据 =========================

// 处理LoRa接收数据（JSON序列化、队列缓存、设备缓存、对时）
void processLoraData() {
  if (!loraReceivedFlag) {
    return;
  }
  loraReceivedFlag = false;
  Serial.print("LORA：");
  Serial.println(loraStr);

  // 1. 构建JSON并存入队列/设备缓存
  StaticJsonDocument<256> doc;
  doc["rssi"] = (int)lastRssi;
  doc["snr"] = (int)lastSnr;
  doc["info"] = loraStr;
  doc["upDateDevice"] = deviceName;
  doc["time"] = getCurrentTime(false);
  doc["ms"] = millis();

  String jsonData;
  serializeJson(doc, jsonData);
  addDataToQueue(jsonData);

  String devId = extractDeviceIdFromInfo(String(loraStr));
  if (devId.length() > 0) {
    updateDeviceCache(devId, jsonData);
  }
  // 4. LORA对时
  String infoStr(loraStr);
  int firstPipeIndex = infoStr.indexOf('|');
  if (firstPipeIndex > 0) {
    int messageType = infoStr.substring(0, firstPipeIndex).toInt();
    // 2. DTU上报
    sendLoraInfoUseDtu(String(loraStr), String(lastRssi), String(lastSnr));
    if (messageType == MSG_TYPE_TIME) {
      sendDownInfo(String(loraStr));
    }
    // 3. 下发回复
    if (messageType == MSG_TYPE_TIME || messageType == MSG_TYPE_SYN_UP_TIME) {
      int secondPipeIndex = infoStr.indexOf('|', firstPipeIndex + 1);
      if (secondPipeIndex > 0) {
        String timeStr = infoStr.substring(secondPipeIndex + 1);
        timeStr = timeStr.substring(0, timeStr.indexOf('|'));
        // Serial.print("⏰ 收到对时信息: ");
        // Serial.print(timeStr);
        if (!haveRightTime() || messageType == MSG_TYPE_SYN_UP_TIME) {
          // Serial.println("✅ 系统更新LORA上报的时间可能有很大误差");
          setTimeFromLora(timeStr);
        }
      }
    }
  }
}

// ========================= 系统初始化 =========================
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);
  Serial2.begin(115200, SERIAL_8N1, 17, 18);
  delay(1000);
  deviceName = makeDivceName();
  initRadio();
  initBLE();
  Serial.println("✅ 系统启动完成   进入监听状态");
  Radio.Rx(0);
}
unsigned long lastUpSelfTm = (30 * 60 * 1000) / 2;
void loop() {

  // 超过10分钟的周期才上报，不要流量溢出
  if ((lastUpSelfTm) < millis()) {
    lastUpSelfTm = millis() + (30 * 60 * 1000);
    // 测试电量
    batterystr = readBatteryEndStr(deviceName);
    String dataStr = String(MSG_TYPE_TIME) + "|" + deviceName + "|" + getCurrentTime(false) + "|" + batterystr;
    dataStr += "|" + String(rtcSendCount++);
    sendLoraInfoUseDtu(dataStr, "0", "0");
  }

  // BLE数据同步发送
  if (needSync && dataCount > 0) {
    delay(50);
    String jsonData = getAndRemoveFirstData();
    StaticJsonDocument<256> newDoc;
    DeserializationError error = deserializeJson(newDoc, jsonData);
    // 3. 检查解析是否成功（重要！）
    if (error) {
      Serial.print(F("JSON 解析失败: "));
      Serial.println(error.c_str());
    } else {
      newDoc["now"] = millis();
      String finalJsonData;
      serializeJson(newDoc, finalJsonData);
      // 打印或发送最终结果
      Serial.println(finalJsonData);
      pCharacteristic->setValue(finalJsonData.c_str());
      pCharacteristic->notify();
      Serial.print("✅ 同步发送：");
      Serial.println(finalJsonData);
      Serial.print("📊 剩余：");
      Serial.println(dataCount);
    }
  }
  if (down_syn_time < millis() && needSyncTimeDeviceid.length() > 0) {
    String dataStr = String(MSG_TYPE_SYN_TIME) + "|" + needSyncTimeDeviceid;
    dataStr += "|" + getCurrentTime(true);
    sendLoraToDeviceid(dataStr);
    needSyncTimeDeviceid = "";
  }

  Radio.IrqProcess();
  processLoraData();
  receiveDtuData();
  delay(100);
}
