/*
 * Heltec Automation - LoRa 纯接收测试 (适配 V3 开发板)
 * 功能：仅 LoRa 接收，无发送、无冗余、极简稳定
 */

#include "LoRaWan_APP.h"
#include "Arduino.h"

// 外部函数声明（确保你的项目中包含了这两个函数的具体实现）
extern void openLedByNum(int count, int delayMs);
extern void showDisplayBy4Area(String loraStatus, String gpsInfo, String sysStatus, String remak);

// ===================== LoRa 参数配置 =====================
#define RF_FREQUENCY 928000000   // 433MHz
#define LORA_BANDWIDTH 0         // 125kHz
#define LORA_SPREADING_FACTOR 9  // SF9
#define LORA_CODINGRATE 1        // 4/5
#define LORA_PREAMBLE_LENGTH 8   // 前导码长度
#define LORA_SYMBOL_TIMEOUT 5    // 符号超时
#define BUFFER_SIZE 30           // 缓冲区大小

// ===================== LoRa 全局变量 =====================
char rxpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;



bool needPlaLed = false;
String loraRecvStr = "";
int16_t rssiValue = 0;

// ===================== 状态枚举 =====================
typedef enum {
  LOWPOWER,
  STATE_RX
} States_t;

States_t state;


// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  // V3 开发板硬件初始化
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);


  // 绑定接收完成回调函数
  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  // 配置 LoRa 接收参数
  // 注意：第5个参数是同步字(Sync Word)。如果你的发送端是 0x12，请在这里把 0 改成 0x12
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, 0, 0, true, 0, 0, false, true);

  state = STATE_RX;
}

// ===================== 主循环 =====================
void loop() {
  delay(1000);
  // 处理 LED 闪烁
  if (needPlaLed) {
    needPlaLed = false;
    openLedByNum(5, 50);
  }
  // 状态机处理
  switch (state) {
    case STATE_RX:
      Radio.Rx(0);  // 开启持续接收模式
      state = LOWPOWER;
      break;

    case LOWPOWER:
      Radio.IrqProcess();  // 持续处理底层 LoRa 中断
      break;

    default:
      break;
  }
}

// ===================== 接收完成回调函数 =====================
// 注意：此函数直接定义在最外层，不需要在顶部额外声明
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  rssiValue = rssi;
  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';  // 确保字符串正常结束
  loraRecvStr = String(rxpacket);

  Serial.print("Received: ");
  Serial.println(loraRecvStr);
  Serial.println( "rssi"+String(rssi)+" snr"+String(snr));


  needPlaLed = true;

  // 提示：Radio.Rx(0) 已经在 loop 的 STATE_RX 状态中重新触发了，
  // 所以这里不需要再次调用 Radio.Rx(0)
}