/*
 * HelTec ESP32 接收端程序 (牛羊GPS定位)
 * 功能：Serial2(DTU)接收数据 + 定时发送数据
 */
#include "Arduino.h"
#include "LoRaWan_APP.h"

// 定义定时器变量
unsigned long previousMillis = 0;
const long interval = 10000;  // 10秒发送一次

// 系统初始化
void setup() {
  Serial.begin(115200);

  // 初始化系统 (注意：SLOW_CLK_TYPE 拼写已修正)

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  delay(1000);

  // 初始化 DTU 串口 (TX: 45, RX: 46)
  // Serial2.begin(9600, SERIAL_8N1, 45, 46);
  Serial2.begin(115200, SERIAL_8N1, 17, 18); 
  delay(1000);

  Serial.println("✅ 系统启动完成，正在监听 Serial2...");
}

/**
 * 接收 Serial2 (DTU) 数据的方法
 */
void receiveDtuData() {
  if (Serial2.available() > 0) {
    while (Serial2.available() > 0) {
      byte incomingByte = Serial2.read();
      if (incomingByte >= 32 && incomingByte <= 126) {
        Serial.print((char)incomingByte);
      }
    }
    Serial.println(); 
    Serial2.flush(); // 【新增】强制清空串口接收缓冲区，防止残留垃圾数据
  }
}

/**
 * 发送 LoRa 信息到 DTU (保持 10秒发送一次的逻辑)
 */
int dataCount = 0;
int lastPacketRssi=-19;
int lastPacketSnr=-88;

// 假设这是你从 LoRa 库中获取到的信号参数
// 如果还没有获取逻辑，可以先用这两个变量测试
extern int lastPacketRssi; 
extern int lastPacketSnr;  

void sendLoraInfoUseDtu() {
  dataCount++;
  
  // 1. 基础定位字符串
  String lorastr = "1|v3-7|0.00000,0.00000|" + String(dataCount);
  String deviceid = "v3-7" ;

  // 2. 构建 JSON 报文 (已彻底修复拼接语法)
  String json = "{"
                "\"id\":" + String(millis()) + "," 
                "\"version\":\"1.0\","
                "\"method\":\"thing.event.property.post\","
                "\"params\":{"
                "\"lorainfo\":\"" + lorastr + "\"," 
                "\"upDateDevice\":\"" + deviceid + "\"," 
                "\"rssi\":" + String(dataCount) + ","
                "\"snr\":" + String(lastPacketSnr) +
                "}"
                "}";

  // 3. 打印并发送
  // Serial.println("📤 上报报文：" + json);
  // Serial2.println(json);
}

void loop() {
  // 1. 持续监听 Serial2 接收数据 (非阻塞，不会漏掉数据)
  receiveDtuData();

  // 2. 使用 millis() 实现非阻塞的 10秒定时发送
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    sendLoraInfoUseDtu();
  }

  delay(100);
}