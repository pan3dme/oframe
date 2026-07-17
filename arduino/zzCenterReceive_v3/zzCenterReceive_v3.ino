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
char sendData[BUFFER_SIZE];  // 发送数据缓存

bool needSendTimeSync = false;
unsigned long scheduledSendMs = 0;  // 定时发送时间点(millis)

bool needPlaLed = false;
bool loraReceivedFlag = false;
volatile int16_t lastRssi = 0;
volatile int8_t lastSnr = 0;
volatile uint16_t lastPayloadSize = 0;

// ========================= 前向声明 =========================
String findLastMessageByDevice(String deviceId);


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
String cmdLoraInfoStr = "";
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
    String targetId = docCom["deviceId"].as<String>();

    if (type == 1) {
      if (cmdLoraInfoStr.length() == 0) {
        cmdLoraInfoStr = "5|" + targetId;  //5用来标记高频
      } else {
        cmdLoraInfoStr += ",";
        cmdLoraInfoStr += targetId;
      }
    }
    Serial.print("组装下发命令 ");
    Serial.println(cmdLoraInfoStr);
  }
  // {"cmd":"setfreq","value":1,"deviceId":"v4-10"}
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
StaticJsonDocument<256> needReadJson;
// LoRa接收回调（仅拷贝数据，耗时操作在主循环处理）
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    memcpy(loraStr, payload, size);
    loraStr[size] = '\0';
    // lastRssi = rssi;
    // lastSnr = snr;
    // lastPayloadSize = size;
    // loraReceivedFlag = true;
    // needPlaLed = true;
    // receiveCount++;


    Serial.println(loraStr);
    // 存入队列并更新设备缓存
    if (size > 0) {
      needReadJson["rssi"] = (int)rssi;
      needReadJson["snr"] = (int)snr;
      needReadJson["info"] = loraStr;
      needReadJson["upDateDevice"] = deviceName;
      needReadJson["time"] = getCurrentTime();

      StaticJsonDocument<256> doc;
      doc["rssi"] = (int)rssi;
      doc["snr"] = (int)snr;
      doc["info"] = loraStr;
      doc["upDateDevice"] = deviceName;
      doc["time"] = getCurrentTime();


      String jsonData;
      serializeJson(doc, jsonData);
      addDataToQueue(jsonData);
      const char *infoRaw = doc["info"];
      if (infoRaw != nullptr) {
        String devId = extractDeviceIdFromInfo(String(infoRaw));
        updateDeviceCache(devId, jsonData);
      }
    }


    //LORA对时
    int firstPipeIndex = String(loraStr).indexOf('|');
    if (firstPipeIndex > 0) {
      int messageType = String(loraStr).substring(0, firstPipeIndex).toInt();
      if (messageType == MSG_TYPE_TIME || messageType == MSG_TYPE_SYNSTIME) {
        int secondPipeIndex = String(loraStr).indexOf('|', firstPipeIndex + 1);
        if (secondPipeIndex > 0) {
          String timeStr = String(loraStr).substring(secondPipeIndex + 1);
          timeStr = timeStr.substring(0, timeStr.indexOf('|'));
          Serial.print("⏰ 收到对时信息: ");
          Serial.println(timeStr);
          if (!haveRightTime() || messageType == MSG_TYPE_SYNSTIME) {
            Serial.println("✅ 系统更新LORA上报的时间可能有很大误差");
            setTimeFromLora(timeStr);
          } else {
            Serial.println("⚠️中继已有时间，不接收普通上报时间");
          }
        }
      }
    }


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

String lastPrintTimeStr = "";  // 存储上次打印的时间字符串（不含毫秒）

void printCurrentTime() {
  String nowStr = getCurrentTime();  // 完整时间字符串 "1970/01/01 00:00:48.679"
  // 提取秒级字符串：去掉毫秒部分（取第一个空格后的前8个字符？实际是日期+时间，要忽略毫秒）
  // 假设格式固定为 "YYYY/MM/DD HH:MM:SS.mmm"，我们需要取到秒
  int dotIndex = nowStr.indexOf('.');
  String nowSecStr = (dotIndex != -1) ? nowStr.substring(0, dotIndex) : nowStr;  // 去掉毫秒
  if (lastPrintTimeStr != nowSecStr) {
    Serial.println(nowStr);  // 或者只打印秒级字符串
    lastPrintTimeStr = nowSecStr;
  }
  if (!haveRightTime()) {
    // Serial.println("❌当前时间还没对时成功");
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
// ========================= 主循环 =========================
unsigned long lastDisplayUpdate = 0;
unsigned long lastrecdLoraTm = 0;
unsigned long lastUpSelfTm = 0;

// ========================= 系统初始化 =========================
void sendDownInfo() {
  String dataStr = "2|" + deviceName;
  dataStr += "|" + getCurrentTime();
  int len = snprintf(sendData, BUFFER_SIZE, "%s", dataStr.c_str());
  if (len < 0 || len >= BUFFER_SIZE) {
    Serial.println("⚠️ 数据过长，已截断");
    sendData[BUFFER_SIZE - 1] = '\0';
  }
  Serial.print(getCurrentTime());
  Serial.print("下发到设备：");
  Serial.print(sendData);
  Serial.print("len:");
  Serial.println(strlen(sendData));
  Radio.Send((uint8_t *)sendData, strlen(sendData));
}
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);
  Serial2.begin(115200, SERIAL_8N1, 17, 18);
  delay(1000);
  deviceName = makeDivceName();
  displayBuf[0] = "id:" + deviceName + " rec";
  initRadio();
  delay(1000);
  initBLE();
  if (haveRightTime()) {
    Serial.print("✅ 系统有正确时间");
    printCurrentTime();
  } else {
    Serial.print("❌ 系统时间不正确");
    printCurrentTime();
  }
  Serial.println("✅ 系统启动完成   进入监听状态");
  Radio.Rx(0);
}

void loop() {

  Radio.IrqProcess();
  receiveDtuData();
  if ((lastUpSelfTm) < millis()) {
    lastUpSelfTm = millis() + SEND_INTERVAL_MS;
    // sendLoraInfoUseDtu(String(MSG_TYPE_BATTERY) + "|" + deviceName + "|1.00|1119|948|5.08|285", "0", "0");
  }
  if (!needReadJson.isNull()) {
    Serial.println("需要处理当前信息");
    Serial.println(needReadJson["info"].as<String>());
    needReadJson.clear();
    delay(1000);
    Radio.Sleep();
    delay(3000);
    sendDownInfo();
 
 
  }

  delay(1000);
}
