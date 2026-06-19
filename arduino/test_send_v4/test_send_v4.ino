// LoRa发送端测试程序 - Heltec V4板子 (已修复高功率模式)
#include <RadioLib.h>

// ================= 硬件引脚定义 (V4专用) =================
#define LORA_NSS    8
#define LORA_DIO1   14
#define LORA_RST    12
#define LORA_BUSY   13

// GC1109 外部功放引脚 (关键！)
#define PA_POWER    7   // 功放主电源
#define PA_EN       2   // 功放使能
#define PA_TX_EN    46  // 发射通路控制

SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// LoRa通信参数配置
#define LORA_FREQ   928.0    // 频率
#define SF_NUM      10       // 扩频因子
#define BW_VAL      125.0    // 带宽
#define CR_NUM      0        // 编码率

int sendCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ==========================================
  // 第一步：必须先开启外部功放 (GC1109)
  // ==========================================
  pinMode(PA_POWER, OUTPUT);
  digitalWrite(PA_POWER, HIGH);    // 1. 给功放供电

  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, HIGH);       // 2. 使能功放芯片

  pinMode(PA_TX_EN, OUTPUT);
  digitalWrite(PA_TX_EN, HIGH);    // 3. 打开发射通路 (常开模式)

  Serial.println("✅ GC1109 外部功放已强制开启");
  delay(500); // 等待电源稳定

  // ==========================================
  // 第二步：初始化 SX1262
  // ==========================================
  int initResult = loraRadio.begin();
  if (initResult != RADIOLIB_ERR_NONE) {
    Serial.print("❌ 射频初始化失败：");
    Serial.println(initResult);
    while (1);
  }

  // V4使用外部GC1109功放，已通过GPIO强制开启TX通路
  // 必须禁用DIO2自动RF开关，避免与手动控制的GPIO冲突
  loraRadio.setDio2AsRfSwitch(false);

  // ==========================================
  // 第三步：配置 LoRa 参数与功率
  // ==========================================
  loraRadio.setFrequency(LORA_FREQ);

  // 设置功率：22dBm + 优化模式 (paDutyCycle=4, hpMax=7)
  loraRadio.setOutputPower(22, true);

  loraRadio.setSpreadingFactor(SF_NUM);
  loraRadio.setBandwidth(BW_VAL);
  loraRadio.setCodingRate(CR_NUM);
  loraRadio.setPreambleLength(8);
  loraRadio.setCRC(true);

  Serial.println("✅ 发射就绪 | 22dBm | SF10 | 928MHz");
}

void loop() {
  String sendContent = "com7 send- " + String(sendCount) + "  ";
  uint8_t sendBuffer[128];
  uint16_t dataLen = sendContent.length();
  sendContent.getBytes(sendBuffer, dataLen);

  Serial.print("发送：");
  Serial.println(sendContent);

  int ret = loraRadio.transmit(sendBuffer, dataLen);

  if (ret == RADIOLIB_ERR_NONE) {
    Serial.println("✅ 发送成功");
  } else {
    Serial.print("❌ 发送错误码：");
    Serial.println(ret);
  }

  sendCount++;
  delay(4000);
}