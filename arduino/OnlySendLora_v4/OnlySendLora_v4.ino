/*
 * Heltec ESP32 LoRa 纯发送程序
 * 功能：每隔10秒自动发送一次LoRa数据
 * 不接收、不处理回执、无接收回调
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <pan3dme.h>


#include "HT_TinyGPS++.h"

// ==================== 引脚枚举定义 ====================
// GPS模块电源控制引脚
#define VGNSS_CTRL 34  // GPS电源控制引脚（高电平关，低电平开）

// GPS串口引脚
#define GPS_RX_PIN 39  // GPS模块TX -> 开发板RX
#define GPS_TX_PIN 38  // GPS模块RX -> 开发板TX

// 其他外设电源控制
#define GPS_ANT_EN 42  // GPS天线电源使能引脚


// GC1109 控制引脚
#define FEM_EN 2
#define FEM_PA 46






String deviceName = "v4-x";
String gpsInfo = "0.00000,0.00000";
String sendStr = "";


// ==================== 全局变量 ====================
char sendData[BUFFER_SIZE];  // 发送数据缓存
RadioEvents_t radioEvents;   // LoRa 事件
String displayBuf[4] = { "", "", "", "" };

// GPS 全局对象
TinyGPSPlus gps;

// 发送状态枚举
typedef enum {
  DEVICE_SLEEP,  // 休眠
  DEVICE_SEND    // 发送
} DeviceState;

int packetCount = 0;       // 数据包编号
DeviceState currentState;  // 当前状态

// 发送计时变量
unsigned long lastSendTime = 0;
const long sendInterval = 60000;  // 发送间隔：5秒

// 仅保留发送相关回调
void onSendDone(void);
void onSendTimeout(void);

// GPS初始化
void initGPS() {
  pinMode(VGNSS_CTRL, OUTPUT);
  digitalWrite(VGNSS_CTRL, LOW);
  pinMode(GPS_ANT_EN, OUTPUT);
  digitalWrite(GPS_ANT_EN, HIGH);
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("GPS 已启动");
}
void readGpsInfo() {


  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();

  if (gps.location.isValid() && gps.time.isValid() && gps.satellites.value() > 0) {
    gpsInfo = String(gps.location.lat(), 5) + "," + String(gps.location.lng(), 5);
  } else {
    gpsInfo = "0.00000,0.00000";
  }

  if (gps.location.isValid() && gps.time.isValid() && gps.satellites.value() > 0) {
    displayBuf[1] = "sat:" + String(gps.satellites.value());
  } else {
    displayBuf[1] = "sat:0";
  }
  displayBuf[2] = gpsInfo;
}
void initLora() {
  radioEvents.TxDone = onSendDone;
  radioEvents.TxTimeout = onSendTimeout;
  // LoRa 初始化
  Radio.Init(&radioEvents);
  Radio.SetChannel(LORA_FREQ);
  Serial.print("✅ 当前lora频段");
  Serial.println(LORA_FREQ);
  // 发送参数配置
  Radio.SetTxConfig(MODEM_LORA, TX_POWER, 0, LORA_BW,
                    LORA_SF, LORA_CR, PREAMBLE_LENGTH, false,
                    true, 0, 0, false, 3000);

  currentState = DEVICE_SLEEP;  // 初始状态：休眠
}
// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  deviceName = makeDivceName();
  
  displayBuf[0] = "name " + deviceName;

#if defined(WIFI_LORA_32_V4)
  initGPS();
  pinMode(FEM_EN, OUTPUT);
  digitalWrite(FEM_EN, HIGH);
#endif

  initLora();

  // 绑定发送事件
}
void sendInfoByType(char* data, int type) {
  sendStr = String(type) + "|" + deviceName;
  // 状态机
  switch (type) {
    //坐标信息
    case 1:
      sendStr = sendStr + "|" + gpsInfo + "|" + String(packetCount);
      break;
    case 2:

      break;
  }
  strcpy(data, sendStr.c_str());
  Serial.print("发送：");
  Serial.println(data);
  Serial.println(strlen(data));
}
// ==================== 主循环 ====================
void loop() {
  openLedByNum(1, 500);
  // 读取GPS
  while (Serial1.available()) {
    gps.encode(Serial1.read());
  }



  // ============== 核心：每5秒触发一次发送 ==============
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    currentState = DEVICE_SEND;
  }

  // 状态机
  switch (currentState) {
    // 发送数据
    case DEVICE_SEND:
      packetCount++;
      readGpsInfo();  // 把字符串装进 gpsData
      sendInfoByType(sendData, 1);
      Radio.Send((uint8_t*)sendData, strlen(sendData));
      openLedByNum(10, 50);
      currentState = DEVICE_SLEEP;  // 发完立即休眠
      displayBuf[3] = "send lora";
      break;
    // 休眠等待
    case DEVICE_SLEEP:
      displayBuf[3] = "lora sleep";
      Radio.IrqProcess();
      break;
  }
  showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);
}

// ==================== 发送完成回调 ====================
void onSendDone(void) {
  Serial.println("发送完成 ✅");
}

// ==================== 发送超时回调 ====================
void onSendTimeout(void) {
  Radio.Sleep();
  Serial.println("发送超时 ❌");
}