/*
 * Heltec Automation LoRa乒乓通信测试代码
 * 功能：两块ESP32 LoRa开发板之间自动互发数据（乒乓测试）
 * 说明：仅使用LoRa底层硬件通信，不使用LoRaWAN协议
 *       两块设备烧录相同程序，即可自动收发测试
 */

#include "LoRaWan_APP.h"  // LoRa底层驱动库
#include "Arduino.h"      // Arduino核心库
#include "HT_TinyGPS++.h"
TinyGPSPlus gps;

// 外部声明：LED闪烁函数（在其他文件实现）
extern void openLedByNum(int count, int delayMs);

constexpr uint32_t RF_FREQUENCY = 433000000;  // 频率：433MHz（国内通用）
constexpr int16_t TX_OUTPUT_POWER = 20;       // 发射功率：20dBm

constexpr uint8_t LORA_BANDWIDTH = 0;          // 带宽：0=125kHz（最稳定）
constexpr uint8_t LORA_SPREADING_FACTOR = 10;  // SF10：更远距离更稳
constexpr uint8_t LORA_CODINGRATE = 1;         // 纠错率：4/6（平衡稳定）

constexpr uint8_t LORA_PREAMBLE_LENGTH = 8;  // 前导码长度
constexpr uint8_t LORA_SYMBOL_TIMEOUT = 5;   // 符号超时

constexpr bool LORA_FIX_LENGTH_PAYLOAD_ON = false;  // 可变长度数据包
constexpr bool LORA_IQ_INVERSION_ON = false;        // 关闭IQ反转

constexpr uint16_t RX_TIMEOUT_VALUE = 1000;  // 接收超时1000ms
constexpr size_t BUFFER_SIZE = 36;           // 缓冲区30字节


#define GPS_RX_PIN 18
#define GPS_TX_PIN 17
const byte disableAuto[] = {0xB5,0x62,0x06,0x3C,0x02,0x00,0x00,0x00,0x3E,0x31};
const byte sleepCmd[]    = {0xB5,0x62,0x06,0x3C,0x02,0x00,0x01,0x01,0x40,0x33};
const byte wakeCmd[]     = {0xB5,0x62,0x06,0x3C,0x02,0x00,0x00,0x01,0x3F,0x32};


/* ==================== 全局变量 ==================== */
char txpacket[BUFFER_SIZE];  // 发送数据缓冲区
char rxpacket[BUFFER_SIZE];  // 接收数据缓冲区

static RadioEvents_t RadioEvents;  // LoRa事件回调结构体

// LoRa事件回调函数声明
void OnTxDone(void);                                                       // 发送完成回调
void OnTxTimeout(void);                                                    // 发送超时回调
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);  // 接收完成回调

// 设备状态枚举：休眠 / 接收 / 发送
typedef enum {
  LOWPOWER,  // 低功耗休眠状态
  STATE_TX   // 发送状态
} States_t;


States_t state;          // 当前设备状态
bool sleepMode = false;  // 休眠模式标志
int16_t Rssi;            // 接收信号强度
int16_t rxSize;          // 接收到的数据长度

const uint64_t allowedDevices[] = {
  0x248B9C697090ULL,
  0x6809A21B5BF8ULL,
  0x8442AAAC85D8ULL,
  0x301BA21B5BF8ULL,
  0x0C46AAAC85D8ULL,
  0x9875555ULL
};

String deviceName = "v3-x";

void makeDivceName() {
  const uint64_t currentId = ESP.getEfuseMac();
  Serial.printf("当前设备编号: %012llX\n", currentId);

  int index = -1;
  for (size_t i = 0; i < sizeof(allowedDevices) / sizeof(allowedDevices[0]); ++i) {
    if (currentId == allowedDevices[i]) {
      index = static_cast<int>(i);
      break;
    }
  }

  if (index >= 0) {
    deviceName = "v3-" + String(index);
    Serial.println("设备认证成功，设备名为: " + deviceName);
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
  }
}

void initRola() {
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  Radio.SetTxConfig(MODEM_LORA,
                    TX_OUTPUT_POWER,
                    0,
                    LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH,
                    LORA_FIX_LENGTH_PAYLOAD_ON,
                    true,
                    0,
                    0,
                    LORA_IQ_INVERSION_ON,
                    3000);

  Radio.SetRxConfig(MODEM_LORA,
                    LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE,
                    0,
                    LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT,
                    LORA_FIX_LENGTH_PAYLOAD_ON,
                    0,
                    true,
                    0,
                    0,
                    LORA_IQ_INVERSION_ON,
                    true);

  state = STATE_TX;
}

/* ==================== 初始化函数（只执行一次） ==================== */
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(1000);
  Serial2.write(disableAuto, sizeof(disableAuto));
  delay(150);
  Serial.println("===== GPS 初始化完成 =====");

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  makeDivceName();
  initRola();
}

/* ==================== 定时打印变量 ==================== */
unsigned long lastPrintTime = 0;
constexpr unsigned long PRINT_INTERVAL_MS = 10000;
int skipNum = 0;

/* ==================== 主循环（反复执行） ==================== */
void loop() {
  openLedByNum(1, 500);  // LED闪烁1次，每次500ms（运行提示）

  while (Serial2.available()) {
    gps.encode(Serial2.read());
  }
  Serial.print("  卫星：");
  Serial.print(gps.satellites.value());
  Serial.println(" ");

  // ============== 每10秒执行一次：打印系统状态 ==============
  unsigned long currentTime = millis();
  if (currentTime - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = currentTime;

    // 如果当前处于休眠状态，强制切换到发送状态
    if (state == LOWPOWER) {
      Radio.Sleep();
      state = STATE_TX;
      Serial.println("【每20秒定时记录】系统运行中...");
    }
  }

  // ============== 根据当前状态执行不同操作 ==============
  switch (state) {

    // -------------------- 状态1：发送数据 --------------------
    case STATE_TX:
      {
        delay(1000);
        skipNum++;

        char latBuf[16] = "0.000000";
        char lngBuf[16] = "0.000000";
        if (gps.location.isValid()) {
          dtostrf(gps.location.lat(), 9, 6, latBuf);
          dtostrf(gps.location.lng(), 10, 6, lngBuf);
        }

        int packetLen = snprintf(txpacket, BUFFER_SIZE, "1|%s|%s,%s|%d", deviceName.c_str(), latBuf, lngBuf, skipNum);
        if (packetLen < 0) {
          packetLen = 0;
        }

        Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, packetLen);
        Radio.Send(reinterpret_cast<uint8_t *>(txpacket), packetLen);
        openLedByNum(10, 50);

        state = LOWPOWER;
        break;
      }



    // -------------------- 状态3：低功耗休眠（处理中断） --------------------
    case LOWPOWER:
      Radio.IrqProcess();  // 处理LoRa中断（接收/发送完成事件）
      break;

    default:
      break;
  }
}

/* ==================== 回调函数：发送完成 ==================== */
void OnTxDone(void) {
  Serial.print("TX done......");  // 串口打印发送完成
                                  // 切换到接收状态，等待对方回复
}

/* ==================== 回调函数：发送超时 ==================== */
void OnTxTimeout(void) {
  Radio.Sleep();                     // LoRa模块休眠
  Serial.print("TX Timeout......");  // 串口提示发送超时
  state = STATE_TX;                  // 发送失败 → 重新进入发送状态
}

/* ==================== 回调函数：成功接收到数据 ==================== */
// payload: 接收到的数据指针
// size: 数据长度
// rssi: 信号强度
// snr: 信噪比
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

  rxSize = size;                    // 保存数据长度
  memcpy(rxpacket, payload, size);  // 将数据复制到接收缓冲区
  rxpacket[size] = '\0';            // 添加字符串结束符
  Radio.Sleep();                    // LoRa模块休眠

  // 串口打印接收信息
  Serial.printf("\r\nreceived packet \"%s\" with rssi %d , length %d\r\n", rxpacket, rssi, rxSize);
  Serial.println("wait to send next packet");

  state = STATE_TX;  // 接收完成 → 切换为发送状态，回复对方
}