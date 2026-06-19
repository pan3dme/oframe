// LoRa发送端测试程序 - Heltec V4板子 (已修复高功率模式)
#include <RadioLib.h>

// ================= 硬件引脚定义 (V4专用) =================
#define LORA_NSS 8
#define LORA_DIO1 14
#define LORA_RST 12
#define LORA_BUSY 13

// GC1109 外部功放引脚 (关键！)
#define PA_POWER 7   // 功放主电源
#define PA_EN 2      // 功放使能
#define PA_TX_EN 46  // 发射通路控制

SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// LoRa通信参数配置
#define LORA_FREQ 928.0  // 频率
#define SF_NUM 10        // 扩频因子
#define BW_VAL 125.0     // 带宽
#define CR_NUM 0         // 编码率

int sendCount = 0;
void setup() {
  Serial.begin(115200);
  delay(1000);

  // 功放电源控制（保持常开）
  pinMode(PA_POWER, OUTPUT);
  digitalWrite(PA_POWER, HIGH);
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, HIGH);
  pinMode(PA_TX_EN, OUTPUT);
  digitalWrite(PA_TX_EN, HIGH);   // 先拉高，等待发送时保持

  delay(500);

  if (loraRadio.begin() != RADIOLIB_ERR_NONE) {
    Serial.println("❌ 射频初始化失败");
    while(1);
  }

  // *** 关键：开启高功率 PA ***
  loraRadio.setPaConfig(0x04, 0x00, 0x00);
  loraRadio.setDio2AsRfSwitch(false);
  loraRadio.setOutputPower(22, true);

  // 其他配置...
  loraRadio.setFrequency(LORA_FREQ);
  loraRadio.setSpreadingFactor(SF_NUM);
  // ...

  Serial.println("✅ 高功率模式已启用");
}

void loop() {
  String sendContent = "com7 send- " + String(sendCount) + "  ";
  uint8_t sendBuffer[128];
  uint16_t dataLen = sendContent.length();
  sendContent.getBytes(sendBuffer, dataLen);

  Serial.print("发送：");
  Serial.println(sendContent);
  digitalWrite(PA_TX_EN, HIGH);  // 3. 打开发射通路 (常开模式)
  delay(100);
  int ret = loraRadio.transmit(sendBuffer, dataLen);
  digitalWrite(PA_TX_EN, LOW);  // 3. 打开发射通路 (常开模式)
  if (ret == RADIOLIB_ERR_NONE) {
    Serial.println("✅ 发送成功");
  } else {
    Serial.print("❌ 发送错误码：");
    Serial.println(ret);
  }

  sendCount++;
  delay(4000);
}