#ifndef pan3dme_h
#define pan3dme_h
 
#include "Arduino.h"
#include <WiFi.h>
#include "HT_SSD1306Wire.h"
#include "HT_TinyGPS++.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ================================== 硬件引脚定义 ==================================
// GPS模块引脚
#define VGNSS_CTRL 34  // GPS电源控制 (低电平开启)
#define GPS_RX_PIN 39  // GPS TX -> ESP32 RX
#define GPS_TX_PIN 38  // GPS RX -> ESP32 TX
#define GPS_ANT_EN 42  // GPS天线电源使能


// ==================== LoRa 通信参数 ====================
#define LORA_FREQ 863000000  // 433MHz 国内通用863 863
#define TX_POWER 20          // 发射功率
#define LORA_BW 0            // 125kHz 带宽
#define LORA_SF 10           // 扩频因子
#define LORA_CR 1            // 纠错率
#define PREAMBLE_LENGTH 8    // 前导码
#define BUFFER_SIZE 36       // 数据缓冲区

// ======================== BLE 配置 ======================= 
#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"





struct BLECallbacks {
  BLEServer *pServer ;
  BLECharacteristic *pCharacteristic ;
}; 
 
BLECallbacks initBLEFun(String deviceName,BLEServerCallbacks *serverCallbacks,BLECharacteristicCallbacks *charCallbacks);
bool initLibWifi(struct tm timeinfo) ;
void openLedByNum(int count, int delayMs)  ;
void showDisplayBy4Area(String a, String b, String c, String d);
void initPanGPS() ;
String getCurrentGpsTm(TinyGPSPlus gps);
String makeDivceName() ;

#endif