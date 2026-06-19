// LoRa发送端测试程序 - Heltec V4板子
#include <RadioLib.h>

// SX1262射频模块引脚定义
#define LORA_NSS    8    // NSS片选
#define LORA_DIO1   14   // DIO1中断
#define LORA_RST    12   // RST复位（V4标准引脚）
#define LORA_BUSY   13   // BUSY忙状态（V4标准引脚）
SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

// GC1109前端功放芯片引脚
#define PA_POWER    7
#define PA_EN       2
#define PA_TX_EN    46

// LoRa通信参数配置
#define LORA_FREQ   928.0    // 工作频率MHz（美版/澳版用915-928，中版改470，欧版改868）
#define SF_NUM      10       // 扩频因子SF10（传输距离远但速率低）
#define BW_VAL      125.0    // 带宽125kHz
#define CR_NUM      0        // 编码率4/5（与pan3dme库保持一致）

int sendCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 配置GC1109功放引脚并开启发射通路
  pinMode(PA_POWER, OUTPUT);
  digitalWrite(PA_POWER, HIGH);    // 开启功放电源
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, HIGH);       // 使能功放芯片
  pinMode(PA_TX_EN, OUTPUT);
  digitalWrite(PA_TX_EN, HIGH);    // 固定为TX发射模式

  Serial.println("GC1109 发射通路常开");

  // 初始化SX1262射频模块
  int initResult = loraRadio.begin();
  if (initResult != RADIOLIB_ERR_NONE) {
    Serial.print("射频初始化失败：");
    Serial.println(initResult);
    while (1);
  }

  // 配置LoRa通信参数
  loraRadio.setFrequency(LORA_FREQ);           // 设置工作频率
  loraRadio.setOutputPower(22, true);          // 输出功率22dBm + 启用外部PA（GC1109）
  loraRadio.setSpreadingFactor(SF_NUM);        // 设置扩频因子
  loraRadio.setBandwidth(BW_VAL);              // 设置带宽
  loraRadio.setCodingRate(CR_NUM);             // 设置编码率
  loraRadio.setPreambleLength(8);              // 前导码长度
  loraRadio.setCRC(true);                      // 启用CRC校验

  Serial.println("发射就绪 22dBm SF10 928MHz");
}

void loop() {
  // 构造测试数据包
  String sendContent = "com7 out- " +String(sendCount)+"  ";
  uint8_t sendBuffer[128];
  uint16_t dataLen = sendContent.length();
  sendContent.getBytes(sendBuffer, dataLen);

  Serial.print("发送：");
  Serial.println(sendContent);

  // 执行数据发送（PA_TX_EN已在setup中固定为HIGH）
  int ret = loraRadio.transmit(sendBuffer, dataLen);

  // 检查发送结果
  if (ret == RADIOLIB_ERR_NONE) {
    Serial.println("✅发送成功");
  } else {
    Serial.print("❌发送错误码：");
    Serial.println(ret);
  }

  sendCount++;
  delay(3000);  // 每3秒发送一次
}