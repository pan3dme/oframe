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



// ================================== BLE 配置 ==================================
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;



bool deviceConnected = false;  // 蓝牙连接状态
bool needSync = false;         // 同步开关：只有收到"true"才发送数据





// ================================== 全局变量 ==================================

TinyGPSPlus gps;                            // GPS对象
struct tm timeinfo;                         // 系统时间结构体
bool wifiTimeSynced = false;                // 是否已成功同步网络时间
time_t syncedEpoch = 0;                     // 成功同步的时间戳
unsigned long syncedMillis = 0;             // 同步时的本地毫秒计数
String displayBuf[4] = { "", "", "", "" };  // 屏幕显示缓冲区

// GPS数据队列 (环形缓冲区模拟)
#define GPS_MAX_COUNT 20
String gpsDataArray[GPS_MAX_COUNT];
int gpsDataCount = 0;
int receiveCount = 0;  // 接收计数器
String deviceName = "v4-x";




#define FEM_EN 2  //FEM总电源 LORA  强化


// ================================== LoRa 参数 ==================================



#define LORA_SYMBOL_TIMEOUT 0

char loraStr[BUFFER_SIZE];

// LoRa状态机
static RadioEvents_t RadioEvents;
bool needPlaLed = false;
bool loraReceivedFlag = false;
bool wifiSyncDone = false;
typedef enum { LOWPOWER,
               STATE_RX } States_t;
States_t state;


// ================================== BLE 回调函数 ==================================
// 连接/断开回调
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("✅ 小程序已连接");
  }
  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    needSync = false;  // 断开连接自动关闭同步
    Serial.println("❌ 断开连接 | 同步已关闭");
    pServer->startAdvertising();
  }
};

// 接收数据回调 (解析JSON指令)
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    Serial.print("📥 收到蓝牙指令：");
    Serial.println(rxValue);

    // 解析 syncing 字段
    if (rxValue.indexOf("\"syncing\":true") != -1 || rxValue.indexOf("syncing:true") != -1) {
      needSync = true;
      Serial.println("✅ 同步已开启：准备发送GPS数据");
    } else if (rxValue.indexOf("\"syncing\":false") != -1 || rxValue.indexOf("syncing:false") != -1) {
      needSync = false;
      Serial.println("⏹️ 同步已关闭");
    }
  }
};

// ================================== 核心逻辑函数 ==================================
// 添加GPS数据到队列，队列满时覆盖最旧数据
void addGpsData(String data) {
  if (gpsDataCount < GPS_MAX_COUNT) {
    gpsDataArray[gpsDataCount++] = data;
  } else {
    // 队列已满，丢弃最旧一条，队尾追加最新数据
    for (int i = 0; i < GPS_MAX_COUNT - 1; i++) {
      gpsDataArray[i] = gpsDataArray[i + 1];
    }
    gpsDataArray[GPS_MAX_COUNT - 1] = data;
  }
}

// 取出并删除队列头部数据 (先进先出)
String getAndRemoveFirstGpsData() {
  if (gpsDataCount == 0) return "";
  String first = gpsDataArray[0];
  for (int i = 0; i < gpsDataCount - 1; i++) {
    gpsDataArray[i] = gpsDataArray[i + 1];
  }
  gpsDataArray[--gpsDataCount] = "";
  return first;
}


bool initWifi() {
  const char *ssid = "yangchang";
  const char *password = "13787501167";

  Serial.print("正在连接 WiFi");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ WiFi 连接失败，跳过网络时间同步");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.println("\n✅ WiFi 连接成功，开始同步网络时间");
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 50) {
    delay(100);
    retry++;
  }

  if (retry >= 50) {
    Serial.println("❌ 网络时间获取失败");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  // 成功获取到网络当前时间
  syncedEpoch = mktime(&timeinfo);
  syncedMillis = millis();
  wifiTimeSynced = true;
  Serial.println("✅ 网络时间同步成功");
  Serial.print("同步时间：");
  Serial.print(timeinfo.tm_year + 1900);
  Serial.print("/");
  Serial.print(timeinfo.tm_mon + 1);
  Serial.print("/");
  Serial.print(timeinfo.tm_mday);
  Serial.print(" ");
  Serial.print(timeinfo.tm_hour);
  Serial.print(":");
  Serial.print(timeinfo.tm_min);
  Serial.print(":");
  Serial.println(timeinfo.tm_sec);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("📶 WiFi 已关闭，后续时间使用本地时钟增量");
  return true;
}


// 获取可用的时间字符串 (优先网络，其次GPS，最后默认)
String getCurrentTime() {
  if (wifiTimeSynced) {
    unsigned long elapsedMs = millis() - syncedMillis;
    time_t currentEpoch = syncedEpoch + elapsedMs / 1000;
    struct tm *tmNow = localtime(&currentEpoch);
    if (tmNow != NULL) {
      char timeStr[30];
      snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d",
               tmNow->tm_year + 1900,
               tmNow->tm_mon + 1,
               tmNow->tm_mday,
               tmNow->tm_hour,
               tmNow->tm_min,
               tmNow->tm_sec);
      return String(timeStr);
    }
  }

  // 如果没有网络时间，则尝试使用 GPS 时间
  if (gps.time.isValid() && gps.date.isValid()) {
    return getCurrentGpsTime();
  }

  return "0000/00/00 00:00:00";
}


// 获取GPS时间 (带自动同步逻辑)
// 获取GPS时间并转换为北京时间 (UTC+8)
String getCurrentGpsTime() {
  return getCurrentGpsTm(gps);  // 返回 月/日 时:分:秒
}

// 初始化GPS串口
void initGPS() {
  initPanGPS();
}

// 初始化BLE服务
void initBLE() {
  BLECallbacks bleCallbacks = initBLEFun(deviceName, new MyServerCallbacks(), new MyCallbacks());
  pServer = bleCallbacks.pServer;
  pCharacteristic = bleCallbacks.pCharacteristic;
}


void showGpsToOled() {
  char gpsStr[128];
  uint16_t randomId = random(10000, 99999);
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  if (gps.location.isValid() && gps.time.isValid() && gps.satellites.value() > 0) {
    sprintf(gpsStr, "ID:%d | TIME:%02d:%02d:%02d | SAT:%d | LAT:%.6f | LON:%.6f",
            randomId, hour, minute, second, gps.satellites.value(),
            gps.location.lat(), gps.location.lng());
    displayBuf[1] = "sat:" + String(gps.satellites.value());
  } else {
    sprintf(gpsStr, "ID:%d | TIME:%02d:%02d:%02d | SAT:0 | LAT:0.000000 | LON:0.000000",
            randomId, hour, minute, second);
    displayBuf[1] = "sat:0 no gps";
  }
}

// LoRa 接收回调函数
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    for (int i = 0; i < size; i++) {
      loraStr[i] = (char)payload[i];
    };
    loraStr[size] = '\0';
    loraReceivedFlag = true;
    needPlaLed = true;
    StaticJsonDocument<200> doc;
    doc["rssi"] = rssi;
    doc["snr"] = snr;
    doc["info"] = loraStr;
    doc["upDateDevice"] = deviceName;
    doc["time"] = getCurrentTime();  // 修正：使用动态时间
    String data;
    serializeJson(doc, data);
    addGpsData(String(data));  // 接收到的数据也存入队列
    receiveCount++;

    displayBuf[2] = "rssi:" + String(rssi) + " snr:" + String(snr);
    Serial.println("rssi:" + String(rssi) + " snr:" + String(snr));
  } else {
    Serial.print("⚠️ LoRa payload too large: ");
    Serial.println(size);
    loraStr[0] = '\0';
  }

  Radio.Rx(0);  // 重新开启接收
}

// 初始化LoRa模块
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(LORA_FREQ);
  Serial.print("✅ 当前lora频段");
  Serial.println(LORA_FREQ);
  Radio.SetRxConfig(MODEM_LORA, LORA_BW, LORA_SF,
                    LORA_CR, 0, PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, false);

  Serial.println("✅ LoRa 初始化完成");

#if defined(WIFI_LORA_32_V4)
  pinMode(FEM_EN, OUTPUT);
  digitalWrite(FEM_EN, HIGH);
#endif



  Radio.Rx(0);
}


// ================================== 主程序 ==================================
void setup() {

  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);
  deviceName = makeDivceName();
  displayBuf[0] = "id:" + deviceName + " rec";
  initWifi();  // 先尝试连WiFi对时
  // initGPS();   // 初始化GPS
  initRadio();  // 初始化LoRa
  initBLE();    // 初始化蓝牙
  Serial.println("✅ 系统启动完成");
}
unsigned long startm = 0;
void loop() {
  startm = millis();
  Radio.IrqProcess();
  // --- LoRa 数据处理 ---
  if (loraReceivedFlag) {
    loraReceivedFlag = false;
    Serial.print("Received LoRa: ");
    Serial.println(loraStr);
    displayBuf[3] = "";
    displayBuf[3].concat(loraStr, 15);
  }

  // --- GPS 数据解析 ---
  if (Serial1.available() > 0) {
    while (Serial1.available()) {
      gps.encode(Serial1.read());
    }
    showGpsToOled();
  }


  // --- LED 提示 ---
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(2, 50);
    Serial.print("✅ 监听一次用时");
    Serial.println(millis() - startm);
  }

  // --- 屏幕显示更新 ---
  displayBuf[0] = "id:" + deviceName + " rec" + String(receiveCount);

  // --- BLE 数据发送逻辑 ---
  if (deviceConnected && needSync && gpsDataCount > 0) {
    String data = getAndRemoveFirstGpsData();
    pCharacteristic->setValue(data.c_str());
    pCharacteristic->notify();

    Serial.print("✅ 同步发送：");
    Serial.println(data);
    Serial.print("📊 剩余：");
    Serial.println(gpsDataCount);
  }


  showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);

  delay(100);
  // --- OLED 屏幕刷新 ---
}