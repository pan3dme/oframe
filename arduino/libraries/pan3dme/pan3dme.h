#ifndef pan3dme_h
#define pan3dme_h

#include "Arduino.h"
#include <cstdint>
#include <time.h>

#include "HT_SSD1306Wire.h"
#include "HT_TinyGPS++.h"
#include "LoRaWan_APP.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ==================== 调试开关 ====================
// 1=开发模式（输出所有调试信息） 0=正式模式（仅输出关键信息）
#define DEBUG_MODE 0

#if DEBUG_MODE
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

// AT+CDKEY=CF673628FFEB926BD918FBA16375615D
//  LoRa消息类型枚举
typedef enum {
  MSG_TYPE_GPS = 1,         // GPS定位信息
  MSG_TYPE_TIME = 2,        // 对时信息
  MSG_TYPE_BATTERY = 3,     // 电量信息（小数，如0.5、0.1）
  MSG_TYPE_SYN_TIME = 4,    // 下发对时信息
  MSG_TYPE_UP_GPS = 5,      //   上报GPS坐标
  MSG_TYPE_CONFIG = 6,      // 时间戳的下发对时
  MSG_TYPE_SYN_UP_TIME = 7, //   上报对时信息
  MSG_TYPE_TIME_REALY = 8,  // 补发对时信息
  MSG_TYPE_COM = 9          // 下载指令到设备
} MessageType_t;

typedef enum {
  FLAG_TYPE_0 = 0,
  FLAG_TYPE_1 = 1,
  FLAG_TYPE_2 = 2,
  FLAG_TYPE_3 = 3
} FlagType_t;

// 前向声明：在头文件中避免包含过多实现细节
class TinyGPSPlus;
class BLEServer;
class BLECharacteristic;
class SSD1306Wire;

// 全局共享变量（在 pan3dme.cpp 中定义）
extern TinyGPSPlus gps;
extern time_t syncedEpoch;
extern unsigned long syncedMillis;

// ================================== 硬件引脚定义
// ================================== GPS模块引脚
#define VGNSS_CTRL 34 // GPS电源控制 (低电平开启)
#if defined(WIFI_LORA_32_V3)
#define GPS_RX_PIN 17
#define GPS_TX_PIN 18
#endif
#if defined(WIFI_LORA_32_V4)
#define GPS_RX_PIN 39 // GPS TX -> ESP32 RX
#define GPS_TX_PIN 38 // GPS RX -> ESP32 TX
#endif
#define GPS_ANT_EN 42 // GPS天线电源使能

// ==================== LoRa 通信参数 ====================
// #define LORA_FREQ 915000000 // 433MHz 国内通用863 863   923  928   915
// #define TX_POWER 22         // 发射功率
#define LORA_BW 0         // 125kHz 带宽
#define LORA_SF 11        // 扩频因子
#define LORA_CR 1         // 纠错率
#define PREAMBLE_LENGTH 8 // 前导码
#define BUFFER_SIZE 48    // 数据缓冲区
#define LORA_SYMBOL_TIMEOUT 0

// ======================== BLE 配置 =======================
#define SERVICE_UUID "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

//========================FEM总电源 LORA  强化========================
// #define LORA_PA_POWER  7
// #define LORA_PA_EN     2
// #define LORA_PA_TX_EN  46

#define VBAT_CTRL_PIN 37 // ADC_Ctrl（控制检测电路开关）
#define VBAT_READ_PIN 1  // VBAT_Read（ADC1_CH0）

const unsigned long SEND_INTERVAL_MS = 1000 * 60 * 5; // 现在设定5分钟一次

static double static_gps_lat = 26.52958;  // 纬度，改成你的值
static double static_gps_lon = 109.39087; // 经度

struct BLECallbacks {
  BLEServer *pServer;
  BLECharacteristic *pCharacteristic;
};

BLECallbacks initBLEFun(String deviceName, BLEServerCallbacks *serverCallbacks,
                        BLECharacteristicCallbacks *charCallbacks);

long long setCSTTime(int year, int mon, int day, int h, int m, int s, int ms);
bool isBoardDateTimeOK();
void hideOLED();
void showOLED();
void openLedByNum(int count, int delayMs);
void showDisplayBy4Area(String a, String b, String c, String d);
void initPanGPS();
bool isReliableGPS();
void setGpsEnable(bool value);
bool getGpsStatus();
void gpsEncode(); // GPS对象
void initPanRadio(RadioEvents_t *radioEvents, int txPower, unsigned long hzFreq,
                  int swNum);
String getGpsInfoStr();
String getCurrentTime(bool includeMillis);
long long getCurrentTimestampSec(); // 获取当前时间戳（秒）
uint32_t getTodaySecond();
void setTimeFromTimestampSec(long long epochMs); // 通过时间戳（秒）设置系统时间
long long mathTimeDiffmsFromSec(long long epochSec);
void printTimestampSec(long long epochSec, const char *label);
void printDurationSec(long long diffMs,
                      const char *label); // 将秒打印为 N小时N分N秒N毫秒
void upDataGpsTimeToCs();
void setTimeFromLora(String timeStr);
int getDevicesIdx();
int getTotalDevices(); // 获取设备总数
String makeDivceName();
int readBatteryEndStr();
bool isTimeInRange(long long timestampSec, const char *timeRangeStr);
int timeWindowToIndex(uint8_t start, uint8_t end);
bool indexToTimeWindow(int idx, uint8_t &outStart, uint8_t &outEnd);
String indexToTwoChar(int idx);
int twoCharToIndex(String str);
void filterGpsByRect(const char *inBuf, char *outBuf, double baseLat,
                     double baseLon, double latHalf, double lonHalf);
void restoreGpsFromDiff(const char *diffBuf, char *outBuf, double baseLat,
                        double baseLon);
bool splitPipeSegment(const char *in, char *out, int idx);
bool replacePipeSegment(const char *src, char *dest, int idx,
                        const char *newVal, size_t destSize);
bool buildFullTimestampStr(const char *segBuf, char *outBuf, size_t outBufLen);

#endif
