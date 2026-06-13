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



// ================================== BLE 配置 ==================================
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;  // 蓝牙连接状态
bool needSync = false;         // 同步开关：只有收到"true"才发送数据
bool showTime = false;


#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// ================================== 硬件引脚定义 ==================================
constexpr uint8_t VGNSS_CTRL = 34;  // GPS电源控制
constexpr uint8_t GPS_RX_PIN = 39;  // GPS TX -> ESP32 RX
constexpr uint8_t GPS_TX_PIN = 38;  // GPS RX -> ESP32 TX
constexpr uint8_t GPS_ANT_EN = 42;  // GPS天线电源使能

// 外部函数声明 (由板载硬件驱动)
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String a, String b, String c, String d);

// ================================== 全局变量 ==================================

TinyGPSPlus gps;                            // GPS对象
struct tm timeinfo;                         // 系统时间结构体
String displayBuf[4] = { "", "", "", "" };  // 屏幕显示缓冲区

// GPS数据队列 (环形缓冲区模拟)
constexpr size_t GPS_MAX_COUNT = 1000;
String gpsDataArray[GPS_MAX_COUNT];
int gpsDataCount = 0;
int receiveCount = 0;  // 接收计数器
String deviceName = "v3-x";

// 设备白名单 (ESP32芯片ID)
uint64_t allowedDevices[] = {
  0x248B9C697090,  // v4
  0x6809A21B5BF8,  // v4
  0x8442AAAC85D8,  // v3
  0x301BA21B5BF8,  // v4
  0x0C46AAAC85D8,  // v3
  0x9875555        // 第2个设备
};

// ================================== LoRa 参数 ==================================
constexpr uint32_t RF_FREQUENCY = 433000000;   // 频率 (433MHz)
constexpr uint8_t LORA_BANDWIDTH = 0;          // 带宽 125kHz
constexpr uint8_t LORA_SPREADING_FACTOR = 10;  // 扩频因子
constexpr uint8_t LORA_CODINGRATE = 1;         // 纠错率
constexpr uint8_t LORA_PREAMBLE_LENGTH = 8;    // 前导码
constexpr uint8_t LORA_SYMBOL_TIMEOUT = 5;     // 符号超时
constexpr size_t BUFFER_SIZE = 36;             // 缓冲区大小
char loraStr[BUFFER_SIZE];

// LoRa状态机
static RadioEvents_t RadioEvents;
bool needPlaLed = false;
bool loraReceivedFlag = false;
bool wifiSyncDone = false;
typedef enum { LOWPOWER,
               STATE_RX } States_t;
States_t state;

// ================================== 函数声明 ==================================
void addGpsData(String data);
String getAndRemoveFirstGpsData();
void initWifi();
String getCurrentTime();
void initBLE();

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void initRadio();
void makeDivceName();

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
// 添加GPS数据到队列
void addGpsData(String data) {

  if (gpsDataCount < GPS_MAX_COUNT) {
    gpsDataArray[gpsDataCount++] = data;
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
#define ssid "yangchang"
#define password "13787501167"
// 初始化WiFi并同步网络时间 (获取后自动断开以省电)
void initWifi() {
  if (wifiSyncDone) return;




  Serial.println("\n========== WiFi 连接开始 ==========");
  Serial.printf("SSID: %s\n", ssid);


  WiFi.disconnect(true);
  delay(100);
  WiFi.eraseAP();
 
  delay(100);
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting ");
  Serial.println(ssid);
  delay(100);

  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  int skipNum = 0;

  // 增加超时时间到 30 秒，并更频繁地打印状态
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
    delay(500);
    Serial.printf("连接中... [%d%%] 状态码: %d\n", skipNum * 2, WiFi.status());
    skipNum++;
    showDisplayBy4Area("wifi connect " + String(skipNum), "SSID: " + String(ssid), "", "");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi 连接成功！");
    Serial.printf("IP 地址: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("信号强度: %d dBm\n", WiFi.RSSI());

    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    Serial.println("正在同步网络时间...");

    delay(2000);

    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 50) {
      delay(100);
      retry++;
    }

    if (retry < 50) {
      Serial.println("✅ 网络时间获取成功！");
      char timeStr[30];
      snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d",
               timeinfo.tm_year + 1900,
               timeinfo.tm_mon + 1,
               timeinfo.tm_mday,
               timeinfo.tm_hour,
               timeinfo.tm_min,
               timeinfo.tm_sec);
      Serial.println("当前时间: " + String(timeStr));
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      Serial.println("📶 WiFi 已关闭以省电");
    } else {
      Serial.println("❌ 获取网络时间失败！");
    }
  } else {
    Serial.printf("\n❌ WiFi 连接失败！最终状态码: %d\n", WiFi.status());
    Serial.println("可能原因：");
    Serial.println("  1. WiFi 密码错误");
    Serial.println("  2. WiFi 信号太弱");
    Serial.println("  3. SSID 不存在");
    Serial.println("  4. WiFi 模块初始化失败");
  }

  wifiSyncDone = true;
}

// 获取可用的时间字符串 (优先网络，其次GPS，最后默认)
String getCurrentTime() {
  if (getLocalTime(&timeinfo)) {
    char timeStr[30];
    snprintf(timeStr, sizeof(timeStr), "%04d/%d/%d %02d:%02d:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
    return String(timeStr);
  }
  return "0000/00/00 00:00:00";
}




// 初始化BLE服务
void initBLE() {
  BLEDevice::init("牛羊GPS" + deviceName);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("✅ 初始化蓝牙完成");
}


// LoRa 接收回调函数
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < BUFFER_SIZE) {
    for (int i = 0; i < size; i++) loraStr[i] = (char)payload[i];
    loraStr[size] = '\0';
    loraReceivedFlag = true;
    needPlaLed = true;
    StaticJsonDocument<200> doc;
    doc["info"] = loraStr;
    doc["upDateDevice"] = deviceName;
    doc["time"] = getCurrentTime();  // 修正：使用动态时间
    String data;
    serializeJson(doc, data);
    addGpsData(String(data));  // 接收到的数据也存入队列
    receiveCount++;
  }
  displayBuf[2] = "rssi:" + String(rssi) + " snr:" + String(snr);
  Serial.println("rssi:" + String(rssi) + " snr:" + String(snr));
  Radio.Rx(0);  // 重新开启接收
}

// 初始化LoRa模块
void initRadio() {
  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, true);
  state = STATE_RX;
  Serial.println("✅ LoRa 初始化完成");
}

// 设备ID认证 (根据MAC地址生成设备名)
void makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  int index = -1;
  for (size_t i = 0; i < sizeof(allowedDevices) / sizeof(allowedDevices[0]); ++i) {
    if (currentId == allowedDevices[i]) {
      index = static_cast<int>(i);
      break;
    }
  }
  if (index != -1) {
    deviceName = "v3-" + String(index);
    Serial.println("设备认证成功，设备名为: " + deviceName);
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
  }
}

// ================================== 主程序 ==================================
void setup() {
  Serial.begin(115200);

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  makeDivceName();
  displayBuf[0] = "id:" + deviceName + " rec";

  initWifi();   // 先尝试连WiFi对时
  initRadio();  // 初始化LoRa
  initBLE();    // 初始化蓝牙
  Serial.println("✅ 系统启动完成 | 同步默认关闭");
}

void loop() {
  static unsigned long lastTimePrint = 0;
  unsigned long now = millis();

  // --- 每秒打印当前时间 ---
  if (now - lastTimePrint >= 1000) {
    lastTimePrint = now;
    Serial.print("[TIME] ");
    Serial.println(getCurrentTime());
  }

  // --- LoRa 状态机处理 ---
  switch (state) {
    case STATE_RX:
      Radio.Rx(0);
      state = LOWPOWER;
      break;
    case LOWPOWER:
      Radio.IrqProcess();
      break;
    default: break;
  }

  // --- LoRa 数据处理 ---
  if (loraReceivedFlag) {
    loraReceivedFlag = false;
    Serial.print("Received LoRa: ");
    Serial.println(loraStr);
    displayBuf[3] = "";
    displayBuf[3].concat(loraStr, 15);
  }



  // --- LED 提示 ---
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(10, 50);
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
    delay(100);  // 防止发送过快
  } else {
    delay(100);
  }



  // --- OLED 屏幕刷新 ---
}