// LoRa接收端测试程序 - Heltec V4板子 (极简版)
#include <RadioLib.h>

// SX1262射频模块引脚定义 (与发射端完全一致)
#define LORA_NSS    8    // NSS片选
#define LORA_DIO1   14   // DIO1中断
#define LORA_RST    12   // RST复位
#define LORA_BUSY   13   // BUSY忙状态
SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// GC1109前端功放芯片引脚 (接收端也需要开启功放)
#define PA_POWER    7
#define PA_EN       2
#define PA_TX_EN    46

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. 配置GC1109功放引脚并开启
  pinMode(PA_POWER, OUTPUT);
  digitalWrite(PA_POWER, HIGH);    // 开启功放电源
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, HIGH);       // 使能功放芯片
  pinMode(PA_TX_EN, OUTPUT);
  digitalWrite(PA_TX_EN, HIGH);    // 开启射频前端通路

  Serial.println("GC1109 接收通路已开启");

  // 2. 初始化SX1262射频模块
  int initResult = loraRadio.begin();
  if (initResult != RADIOLIB_ERR_NONE) {
    Serial.print("射频初始化失败：");
    Serial.println(initResult);
    while (1);
  }

  // V4使用外部GC1109功放，已通过GPIO强制开启通路
  // 必须禁用DIO2自动RF开关，避免与手动控制的GPIO冲突
  loraRadio.setDio2AsRfSwitch(false);

  // 3. 配置LoRa通信参数 (必须与发射端完全一致)
  loraRadio.setFrequency(928.0);       // 工作频率 928MHz
  loraRadio.setOutputPower(22, true);  // 输出功率22dBm + 启用外部PA
  loraRadio.setSpreadingFactor(10);    // 扩频因子 SF10
  loraRadio.setBandwidth(125.0);       // 带宽 125kHz
  loraRadio.setCodingRate(0);          // 编码率 4/5
  loraRadio.setPreambleLength(8);      // 前导码长度
  loraRadio.setCRC(true);              // 启用CRC校验

  Serial.println("接收就绪 22dBm SF10 928MHz");
}

void loop() {
  uint8_t rxBuffer[128];

  // 阻塞等待接收数据
  int ret = loraRadio.receive(rxBuffer, sizeof(rxBuffer));

  // 检查接收结果
  if (ret == RADIOLIB_ERR_NONE) {
    // 获取实际接收到的数据长度
    int len = loraRadio.getPacketLength();
    
    Serial.print("✅ 收到数据 (RSSI: ");
    Serial.print(loraRadio.getRSSI());
    Serial.print(" dBm, SNR: ");
    Serial.print(loraRadio.getSNR());
    Serial.println(" dB):");
    
    // 将接收到的字节转为字符串并打印
    String message = String((char*)rxBuffer).substring(0, len);
    Serial.println(message);
    
  } else if (ret == RADIOLIB_ERR_RX_TIMEOUT) {
    // 接收超时是正常现象，无需报错，继续等待即可
    
  } else {
    // 其他错误
    Serial.print("❌ 接收错误码：");
    Serial.println(ret);
  }
}