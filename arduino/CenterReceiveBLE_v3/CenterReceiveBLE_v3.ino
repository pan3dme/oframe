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
#include "esp_task_wdt.h"
HardwareSerial *dtuSerial;
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

int recelveIdx = 1;
int rtcSendCount = 0;
String batterystr = "";
String deviceName = "x-x";
// 必须要上报GPS时间段
long mustrefrishgpsTime = 0;

// ========================= LoRa全局变量 =========================
char loraStr[BUFFER_SIZE];
char sendData[BUFFER_SIZE];  // 发送数据缓存

// 回调标记（主循环根据此标志处理数据）
bool loraReceivedFlag = false;
int16_t lastRssi = 0;
int8_t lastSnr = 0;

StaticJsonDocument<200> docCom;  // BLE指令解析用（全局复用）

// 晶振偏差追踪
unsigned long lastSyncMillis = 0;  // 上次对时时的 millis()
time_t lastSyncEpoch = 0;          // 上次对时后的系统 epoch
bool hasLastSync = false;          // 是否已有上次记录



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

#define TARGET_ID_MAX 60
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

String targetGpsList[TARGET_ID_MAX];
int targetGpsCount = 0;
// 检查 targetId 是否已存在
bool isTargetGpsExist(String id) {
  for (int i = 0; i < targetGpsCount; i++) {
    if (targetGpsList[i] == id)
      return true;
  }
  return false;
}

// 添加 targetId（若已存在则忽略）
void addTargetGps(String id) {
  if (id.length() == 0)
    return;
  if (isTargetGpsExist(id)) {
    Serial.println("⚠️ targetGps 已存在，忽略添加");
    return;
  }
  if (targetGpsCount < TARGET_ID_MAX) {
    targetGpsList[targetGpsCount++] = id;
    Serial.print("✅ 已添加 targetGps: ");
    Serial.println(id);
  } else {
    Serial.println("❌ targetGps 列表已满！");
  }
}
String getGpsTargetByDeviceid(String deviceId) {
  for (int i = 0; i < targetGpsCount; i++) {
    deserializeJson(docCom, targetGpsList[i]);
    if (docCom.containsKey("deviceId")) {
      String targetId = docCom["deviceId"].as<String>();
      if (targetId == deviceId) {  // 只处理发给当前设备的指令
        return targetGpsList[i];
      }
    }
  }
  return "";
}
void removeTargetGpsByDeviceId(String deviceId) {
  for (int i = targetGpsCount - 1; i >= 0; i--) {
    deserializeJson(docCom, targetGpsList[i]);
    if (docCom.containsKey("cmd") && docCom.containsKey("deviceId")) {
      String targetId = docCom["deviceId"].as<String>();
      if (targetId == deviceId) {
        removeTargetGps(targetGpsList[i]);
      }
    }
  }
}
// 删除指定的 targetId
void removeTargetGps(String id) {
  for (int i = 0; i < targetGpsCount; i++) {
    if (targetGpsList[i] == id) {
      // 将后面的元素前移
      for (int j = i; j < targetGpsCount - 1; j++) {
        targetGpsList[j] = targetGpsList[j + 1];
      }
      targetGpsCount--;
      Serial.print("✅ 已删除 targetGps: ");
      Serial.println(id);
      return;
    }
  }
  Serial.print("⚠️ 未找到 targetGps: ");
  Serial.println(id);
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

    String deviceId = docCom["deviceId"].as<String>();
    if (deviceId == deviceName) {

      String cmd = docCom["cmd"].as<String>();
      String tmp = docCom["value"].as<String>();
      if (cmd == "lorasw") {
        int sw = tmp.toInt();
        if (sw == 1) {
          Radio.Rx(0);
          Serial.print("✅ROLA 开始接收-");
        } else {
          Radio.Sleep();
          Serial.print("❌ROLA进入休眠-");
        }

      } else if (cmd == "relaytimeloc") {
        recelveIdx = tmp.toInt();
        if (recelveIdx == 0) {
          Serial.print("❌不下发对时-");
        } else {
          Serial.print("✅中继下发位置");
          Serial.println(recelveIdx);
        }


      } else if (cmd == "ledsw") {
        int sw = tmp.toInt();
        if (sw == 1) {
          Serial.print("✅开led 灯-");
        } else {

          Serial.print("❌关led 灯-");
        }

      } else if (cmd == "relayreboot") {
        int sw = tmp.toInt();
        if (sw == 1) {
          ESP.restart();
        }
      } else if (cmd == "txpower") {
        int power = tmp.toInt();
        if (power > 10 && power <= 28) {
          initRadio(power);
        }

      } else if (cmd == "refrishgps") {
        // int valueNum = docCom["value"].as<int>();
        // Serial.print("刷新中继连接所有 设备GPS valueNum=");
        // Serial.println(valueNum);
        // mustrefrishgpsTime = millis() + valueNum * 60 * 1000;
      } else {
        Serial.println(
          "❌❌❌下发的对象就是中继， 还没有设计指令功能❌❌❌");
      }
      Serial.println(getCurrentTime(true));
    } else {
      // 先移除原来对应设备的
      removeTargetByDeviceId(deviceId);
      addTargetId(rxValue);
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
void initRadio(int power) {
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  initPanRadio(&RadioEvents, power);
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
  dtuSerial->println(json);
}
// 接收 Serial2 (DTU) 数据，拆分多个拼接JSON并提取cominfo
String signalRss = "0";
void receiveDtuData() {
  String raw = "";

  if (dtuSerial->available() <= 0) {
    return;
  }
  // 1. 读取本次全部数据（限制最大长度防止内存耗尽）
  int rawLen = 0;
  while (dtuSerial->available() > 0 && rawLen < 1024) {
    raw += (char)dtuSerial->read();
    rawLen++;
    delay(2);  // 等待下一个字节
  }
  // 丢弃超出部分
  while (dtuSerial->available() > 0) {
    dtuSerial->read();
  }

  dtuSerial->flush();
  // Serial.print("dtu 返回 -");
  // Serial.println(raw);

  // 解析DTU返回的网络时间: config,nettime,ok,2026,7,31,1,50,6,5
  // 格式: 年,月,日,时,分,秒,毫秒
  if (raw.indexOf("config,csq,ok,") != -1) {
    int lastComma = raw.lastIndexOf(',');
    if (lastComma != -1) {
      String valueStr = raw.substring(lastComma + 1);
      valueStr.trim();  // 去除换行和空格
      int signal = valueStr.toInt();
      int percentage = map(signal, 0, 31, 0, 100);  // 使用 Arduino 的 map 函数
      Serial.print("信号强度值: ");
      Serial.println(percentage);
      signalRss = String(percentage);
    }
  }
  if (raw.indexOf("config,nettime,ok,") != -1) {
    String timePart =
      raw.substring(raw.indexOf("config,nettime,ok,") + 18);  // 跳过前缀
    int ntYear = 0, ntMon = 0, ntDay = 0, ntH = 0, ntM = 0, ntS = 0, ntMs = 0;
    sscanf(timePart.c_str(), "%d,%d,%d,%d,%d,%d,%d", &ntYear, &ntMon, &ntDay,
           &ntH, &ntM, &ntS, &ntMs);
    Serial.printf("DTU网络时间: %4d/%d/%d %02d:%02d:%02d.%03d\n", ntYear, ntMon,
                  ntDay, ntH, ntM, ntS, ntMs);

    long long localEpochMs = getCurrentTimestampMs();

    // 计算DTU时间戳
    struct tm dtuTm = { 0 };
    dtuTm.tm_year = ntYear - 1900;
    dtuTm.tm_mon = ntMon - 1;
    dtuTm.tm_mday = ntDay;
    dtuTm.tm_hour = ntH;
    dtuTm.tm_min = ntM;
    dtuTm.tm_sec = ntS;
    dtuTm.tm_isdst = 0;
    long long dtuEpochMs = (long long)mktime(&dtuTm) * 1000LL + ntMs;
    long long diffMs = dtuEpochMs - localEpochMs;

    printTimestampMs(localEpochMs, "本机时间: ");
    printTimestampMs(dtuEpochMs, "DTU时间: ");
    Serial.printf("时间差: %lld 毫秒 (%s)\n", diffMs >= 0 ? diffMs : -diffMs,
                  diffMs >= 0 ? "DTU快" : "本机快");
    printDurationMs(diffMs >= 0 ? diffMs : -diffMs, "时间差: ");

    setCSTTime(ntYear, ntMon, ntDay, ntH, ntM, ntS, ntMs);
    return;
  }
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
          Serial.print("receiveDtu解析失败: ");
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
  Serial.print("下发lora：");
  Serial.print(sendData);
  Serial.print("len:");
  Serial.println(strlen(sendData));
  Radio.Send((uint8_t *)sendData, strlen(sendData));
}
String needSyncTimeDeviceid;
long down_syn_time = 0;
// ========================= 下发指令 =========================
void sendDownInfo(String loraStr, String deviceId) {
  if (recelveIdx == 0) {
    Serial.println("❌不下发指令，用于测试，中继互不干扰的测试");
    return;
  }

  String dataStr = "";
  String selectCmdStr = getGpsTargetByDeviceid(deviceId);
  bool lastCmd = false;
  if (selectCmdStr.length() > 0) {
    lastCmd = true;
    // Serial.print("❌上次需要上报GPS的信息还没收到过这次重新下发GPS更新指令");
  } else {
    lastCmd = false;
    for (int i = 0; i < targetIdCount; i++) {
      deserializeJson(docCom, targetIdList[i]);
      if (docCom.containsKey("deviceId")) {
        String targetId = docCom["deviceId"].as<String>();
        if (targetId == deviceId) {  // 只处理发给当前设备的指令
          selectCmdStr = targetIdList[i];
          removeTargetId(selectCmdStr);
          break;  // 找到第一个即停止，或改为处理所有
        }
      }
    }
  }

  if (selectCmdStr.length() > 0) {
    Serial.print("✅标记了下发数据");
    Serial.println(selectCmdStr);
    String cmd = docCom["cmd"].as<String>();
    String value = docCom["value"].as<String>();

    dataStr = String(MSG_TYPE_COM) + "|" + deviceId + "|" + cmd + "|" + value;
    sendLoraToDeviceid(dataStr);

  } else {
    needSyncTimeDeviceid = deviceId;
    down_syn_time = millis() + 2000 * recelveIdx;
  }
}


// ========================= 主循环处理LoRa数据 =========================

// 处理LoRa接收数据（JSON序列化、队列缓存、设备缓存、对时）
void processLoraData() {
  if (!loraReceivedFlag) {
    return;
  }
  loraReceivedFlag = false;
  Serial.print("接收lora：");
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
    sendLoraInfoUseDtu(infoStr, String(lastRssi), String(lastSnr));

    if (messageType == MSG_TYPE_TIME || messageType == MSG_TYPE_GPS) {

      if (mustrefrishgpsTime > millis()) {
        Serial.print("mustrefrishgpsTime-");
        Serial.print(mustrefrishgpsTime);
        Serial.print("-");
        Serial.println(millis());
        Serial.print(devId);
        Serial.println("必须经上报GPS坐标");
        sendLoraToDeviceid(String(MSG_TYPE_COM) + "|" + devId + "|upgps|0");
      } else {
        sendDownInfo(infoStr, devId);
      }
    }
    // 3. 下发回复
  }
}


// ========================= 系统初始化 =========================
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);
  deviceName = makeDivceName();
  delay(1000);

#if defined(WIFI_LORA_32_V3)
  Serial2.begin(115200, SERIAL_8N1, 17, 18);
  dtuSerial = &Serial2;
  Serial.println("✅ v3 板子 DTU");
  // dtuSerial->println("V3 DTU TEST");
  delay(1000);
#endif
#if defined(WIFI_LORA_32_V4)
  Serial2.begin(115200, SERIAL_8N1, 38, 39);  // RX=38, TX=39
  dtuSerial = &Serial2;
  Serial.println("✅ v4 板子 DTU");
  // dtuSerial->println("V4 DTU TEST");
  delay(1000);
#endif



  initRadio(22);
  initBLE();

  // 启用任务看门狗，30秒超时自动重启
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = 30 * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true  // 超时触发重启并打印原因
  };
  esp_task_wdt_init(&wdtConfig);
  esp_task_wdt_add(NULL);  // 监控主循环任务

  Serial.println("✅ 系统启动完成   进入监听状态");
  Radio.Rx(0);
}

// 1分钟就上报中继在线时间
unsigned long lastUpSelfTm = 15 * 1000;
unsigned long nextSyncTm = 10 * 1000;
unsigned long CENTEN_INTERVAL_MS = 1000 * 60 * 30;
void loop() {

  // 超过10分钟的周期才上报，不要流量溢出

  if (nextSyncTm < millis()) {
    // 请求网络时间
    nextSyncTm = millis() + CENTEN_INTERVAL_MS;
    dtuSerial->println("config,get,csq");
    delay(200);
    dtuSerial->println("config,get,nettime");
  }
  if ((lastUpSelfTm) < millis()) {
    lastUpSelfTm = millis() + CENTEN_INTERVAL_MS;
    // 测试电量
    batterystr = readBatteryEndStr(deviceName);
    String dataStr = String(MSG_TYPE_TIME) + "|" + deviceName + "|" + getCurrentTime(false) + "|" + batterystr;
    dataStr += "|" + String(rtcSendCount++);
    sendLoraInfoUseDtu(dataStr, signalRss, "0");
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
      if (pCharacteristic != NULL) {
        pCharacteristic->setValue(finalJsonData.c_str());
        pCharacteristic->notify();
      } else {
        Serial.println("❌ BLE特征值未初始化，跳过通知");
        needSync = false;
      }
      Serial.print("✅ 同步发送：");
      Serial.println(finalJsonData);
      Serial.print("📊 剩余：");
      Serial.println(dataCount);
    }
  }
  if (down_syn_time < millis() && needSyncTimeDeviceid.length() > 0) {
    String dataStr = String(MSG_TYPE_SYN_TIME) + "|" + needSyncTimeDeviceid;
    dataStr += "|" + getCurrentTime(true) + "|" + deviceName;

    //     String dataStr = String(MSG_TYPE_SYN_TIME_COPY) + "|" + needSyncTimeDeviceid;
    // dataStr += "|" +String( getCurrentTimestampMs()) + "|" + deviceName;


    
    sendLoraToDeviceid(dataStr);
    needSyncTimeDeviceid = "";
  }

  Radio.IrqProcess();
  processLoraData();
  receiveDtuData();
  esp_task_wdt_reset();  // 喂狗：告诉看门狗系统还活着
  delay(100);
}
