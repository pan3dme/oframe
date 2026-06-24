#ifndef pan3dme_h
#define pan3dme_h

#include "Arduino.h"
#include <time.h>
#include <WiFi.h>
#include "HT_SSD1306Wire.h"
#include "HT_TinyGPS++.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "LoRaWan_APP.h"
//AT+CDKEY=CF673628FFEB926BD918FBA16375615D
// LoRa消息类型枚举
typedef enum {
  MSG_TYPE_GPS = 1,        // GPS定位信息
  MSG_TYPE_TIME = 2,       // 对时信息
  MSG_TYPE_BATTERY = 3,    // 电量信息（小数，如0.5、0.1）
  MSG_TYPE_FIRMWARE = 10,   // 固件更新指令
  MSG_TYPE_COM = 11   // 下载指令到设备
} MessageType_t;

// 前向声明：在头文件中避免包含过多实现细节
class TinyGPSPlus;
class BLEServer;
class BLECharacteristic;
class SSD1306Wire;

// 全局共享变量（在 pan3dme.cpp 中定义）
extern TinyGPSPlus gps;
extern bool wifiTimeSynced;
extern time_t syncedEpoch;
extern unsigned long syncedMillis;

// ================================== 硬件引脚定义 ==================================
// GPS模块引脚
#define VGNSS_CTRL 34 // GPS电源控制 (低电平开启)
#if defined(WIFI_LORA_32_V3)
#define GPS_RX_PIN 45
#define GPS_TX_PIN 46
#endif
#if defined(WIFI_LORA_32_V4)
#define GPS_RX_PIN 39 // GPS TX -> ESP32 RX
#define GPS_TX_PIN 38 // GPS RX -> ESP32 TX
#endif
#define GPS_ANT_EN 42 // GPS天线电源使能

// ==================== LoRa 通信参数 ====================
#define LORA_FREQ 433000000 // 433MHz 国内通用863 863  928
#define TX_POWER 22         // 发射功率
#define LORA_BW 0           // 125kHz 带宽
#define LORA_SF 10          // 扩频因子
#define LORA_CR 1           // 纠错率
#define PREAMBLE_LENGTH 8   // 前导码
#define BUFFER_SIZE 36      // 数据缓冲区
#define LORA_SYMBOL_TIMEOUT 0

// ======================== BLE 配置 =======================
#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

//========================FEM总电源 LORA  强化========================
#define LORA_PA_POWER  7
#define LORA_PA_EN     2
#define LORA_PA_TX_EN  46


#define VBAT_CTRL_PIN 37  // ADC_Ctrl（控制检测电路开关）
#define VBAT_READ_PIN 1   // VBAT_Read（ADC1_CH0）



const unsigned long SEND_INTERVAL_MS = 60000;  // 总周期10秒

struct BLECallbacks
{
  BLEServer *pServer;
  BLECharacteristic *pCharacteristic;
};

BLECallbacks initBLEFun(String deviceName, BLEServerCallbacks *serverCallbacks, BLECharacteristicCallbacks *charCallbacks);
bool initLibWifi();
void disConnectWifi();
void openLedByNum(int count, int delayMs);
void showDisplayBy4Area(String a, String b, String c, String d);
void initPanGPS();
void gpsEncode(); // GPS对象
void initPanRadio(RadioEvents_t* radioEvents);
String getGpsInfoStr();
String getCurrentTime();
bool hasValidTime();
void setTimeFromLora(String timeStr);
int getDevicesIdx();
int getTotalDevices();  // 获取设备总数
String makeDivceName();

#endif
