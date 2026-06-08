/*
 * Heltec Automation - LoRa 纯接收测试
 * 功能：仅 LoRa 接收，无发送、无冗余、极简稳定
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"

// 外部函数
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String a, String b, String c, String d);

// ===================== LoRa 参数 =====================
#define RF_FREQUENCY 928000000
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 10
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 5
#define BUFFER_SIZE 30


// GC1109 控制引脚
#define FEM_EN 2
#define FEM_PA 46

uint64_t allowedDevices[] = {
  0x248B9C697090,  // 第0个设备  v4
  0x6809A21B5BF8,  // 第1个设备  v4
  0x8442AAAC85D8,  // 第1个设备  v3
  0x301BA21B5BF8,  // 第1个设备  v4
  0x9875555        // 第2个设备
};
String deviceName = "v3-x";

// 显示数组
String displayBuf[4] = { "", "", "", "" };
// ===================== LoRa 变量 =====================
char rxpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

bool needPlaLed = false;



// ===================== 状态枚举 =====================
typedef enum {
  LOWPOWER,
  STATE_RX
} States_t;

States_t state;


void makeDivceName() {
  uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  // 3. 遍历数组，查找当前编号在数组中的索引位置
  int index = -1;  // 默认值为 -1，表示未找到
  for (int i = 0; i < sizeof(allowedDevices); i++) {
    if (currentId == allowedDevices[i]) {
      index = i;
      break;  // 找到了就跳出循环
    }
  }
  // 4. 根据索引位置给设备取名
  if (index != -1) {
    deviceName = "v4-" + String(index);  // 拼接成 "id0", "id1" 等
    Serial.println("设备认证成功，设备名为: " + deviceName);
    // 5. (可选) 将这个名字设置为 WiFi 的主机名，方便在路由器后台查看
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
    // 你可以在这里添加处理逻辑，比如让设备进入报错状态或停止运行
  }
}
void initRola() {
  // 绑定接收回调
  RadioEvents.RxDone = OnRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  // 配置接收
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, true);

  state = STATE_RX;
}
// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  makeDivceName();

  displayBuf[0] = "id:" + deviceName + " only rec";

  initRola();
  pinMode(FEM_EN, OUTPUT);
  digitalWrite(FEM_EN, HIGH);
  pinMode(FEM_PA, OUTPUT);
  digitalWrite(FEM_PA, HIGH);
}

// ===================== 主循环 =====================
void loop() {
  delay(1000);
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(5, 50);
  }

  // 屏幕显示
  showDisplayBy4Area(displayBuf[0], displayBuf[1], displayBuf[2], displayBuf[3]);

  switch (state) {
    case STATE_RX:
      Radio.Rx(0);
      state = LOWPOWER;
      break;

    case LOWPOWER:
      Radio.IrqProcess();
      break;

    default:
      break;
  }
}
int runCount = 0;        // 运行计数器
String receiveStr = "";  //接收到的字符串
String receiveArr[5];
// ===================== 接收完成回调 =====================
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';

  receiveStr = String(rxpacket);

  Serial.print("Received: ");
  Serial.println(receiveStr + " rssi:" + String(rssi));
  runCount++;
  displayBuf[1] = "rssi:" + String(rssi) + "  snr:" + String(snr);
  displayBuf[2] = receiveStr.substring(0, 15);  
  displayBuf[3] = receiveStr.substring(15, 30); 

  int start = 0;
  int count = 0;
  int comma;
  do {
    comma = receiveStr.indexOf('|', start);
    if (comma == -1) {
      receiveArr[count++] = receiveStr.substring(start);
    } else {
      receiveArr[count++] = receiveStr.substring(start, comma);
    }
    start = comma + 1;
  } while (comma != -1 && count < 10);


  int type = receiveArr[0].toInt();
  switch (type) {
    //坐标信息
    case 1:

      break;
    case 2:
      break;
  }




  needPlaLed = true;
}