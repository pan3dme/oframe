/*
 * LoRa 发送端 - Heltec 官方库（高性能）
 */
#include "LoRaWan_APP.h"
#include "Arduino.h"

RadioEvents_t radioEvents;
char sendData[] = "Hello LoRa!";
int count = 0;

// 发送完成回调
void onTxDone(void) {
  Serial.println("✅ 发送成功");
  Radio.Sleep();
}

// 发送超时回调
void onTxTimeout(void) {
  Serial.println("❌ 发送超时");
  Radio.Sleep();
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
  digitalWrite(46, LOW);  // TX 时由 Radio 库自动控制
  
  Serial.println("⚡ V4 外部功放已开启");
  
  // 初始化 MCU
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  
  // 配置事件回调
  radioEvents.TxDone = onTxDone;
  radioEvents.TxTimeout = onTxTimeout;
  
  // 初始化 Radio
  Radio.Init(&radioEvents);
  Radio.SetChannel(928000000);  // 928 MHz (美版/中国版)
  
  // 配置发送参数（远距离模式）
  Radio.SetTxConfig(MODEM_LORA, 22, 0, LORA_BW_125, 
                    LORA_SF10, LORA_CR_4_5, 8, 
                    false, true, 0, 0, false, 3000);
  
  Serial.println("✅ 发送端就绪 (22dBm, SF10, BW125)");
}

void loop() {
  // 更新数据包
  sprintf(sendData, "Hello LoRa! #%d", count++);
  
  Serial.print("📤 发送: ");
  Serial.println(sendData);
  
  // 执行发送
  Radio.Send((uint8_t*)sendData, strlen(sendData));
  
  // 处理 Radio 事件
  while (digitalRead(RADIO_DIO_1) == HIGH) {
    Radio.IrqProcess();
  }
  
  delay(3000);
}
