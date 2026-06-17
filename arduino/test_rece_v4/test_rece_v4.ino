/*
 * LoRa 接收端 - Heltec 官方库（高性能）
 */
#include "LoRaWan_APP.h"
#include "Arduino.h"

RadioEvents_t radioEvents;
uint8_t recvBuffer[256];
uint8_t recvSize = 0;

// 接收完成回调
void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Serial.println("-----------------------------------");
  Serial.print("📥 收到: ");
  Serial.println((char*)payload);
  Serial.print("📶 RSSI: ");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.print("📊 SNR: ");
  Serial.print(snr);
  Serial.println(" dB");
  Serial.println("-----------------------------------");
  
  // 重新启动接收
  Radio.Rx(0);  // 连续接收模式
}

// 接收超时回调
void onRxTimeout(void) {
  // 超时后重新启动接收
  Radio.Rx(0);
}

// CRC 错误回调
void onRxError(void) {
  Serial.println("❌ CRC 错误");
  Radio.Rx(0);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // ⚠️ 关键：手动开启 V4 外部功放（FEM）
  pinMode(7, OUTPUT);   // LORA_PA_POWER
  digitalWrite(7, HIGH);
  pinMode(2, OUTPUT);   // LORA_PA_EN
  digitalWrite(2, HIGH);
  pinMode(46, OUTPUT);  // LORA_PA_TX_EN
  digitalWrite(46, LOW);  // 接收时关闭
  
  Serial.println("⚡ V4 外部功放已开启（接收模式）");
  
  // 初始化 MCU
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  
  // 配置事件回调
  radioEvents.RxDone = onRxDone;
  radioEvents.RxTimeout = onRxTimeout;
  radioEvents.RxError = onRxError;
  
  // 初始化 Radio
  Radio.Init(&radioEvents);
  Radio.SetChannel(928000000);  // 928 MHz (美版/中国版)
  
  // 配置接收参数（与发送端一致）
  Radio.SetRxConfig(MODEM_LORA, LORA_BW_125, LORA_SF10, 
                    LORA_CR_4_5, 0, 8, 
                    0, false, 
                    0, true, 0, 0, false, true);
  
  // 启动连续接收
  Radio.Rx(0);
  
  Serial.println("✅ 接收端就绪 (SF10, BW125)");
}

void loop() {
  // 处理 Radio 事件
  Radio.IrqProcess();
}
