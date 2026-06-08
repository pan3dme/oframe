/*
 * Heltec Automation LoRa乒乓通信测试代码
 * 功能：两块ESP32 LoRa开发板之间自动互发数据（乒乓测试）
 * 说明：仅使用LoRa底层硬件通信，不使用LoRaWAN协议
 *       两块设备烧录相同程序，即可自动收发测试
 */

#include "LoRaWan_APP.h"  // LoRa底层驱动库
#include "Arduino.h"      // Arduino核心库

// 外部声明：LED闪烁函数（在其他文件实现）
extern void openLedByNum(int count, int delayMs);
#define RF_FREQUENCY 433000000  // 频率：433MHz（国内通用）

#define TX_OUTPUT_POWER 20  // 发射功率：10dBm（近距离不用大）

#define LORA_BANDWIDTH 0          // 带宽：0=125kHz（最稳定）
#define LORA_SPREADING_FACTOR 10  // SF7：近距离最快最稳（关键！）
#define LORA_CODINGRATE 1         // 纠错率：4/6（平衡稳定）

#define LORA_PREAMBLE_LENGTH 8  // 前导码：8（近距离足够，短而快）
#define LORA_SYMBOL_TIMEOUT 5   // 符号超时：5（防止接收卡死）

#define LORA_FIX_LENGTH_PAYLOAD_ON false  // 可变长度数据包
#define LORA_IQ_INVERSION_ON false        // 关闭IQ反转

#define RX_TIMEOUT_VALUE 1000  // 接收超时1000ms
#define BUFFER_SIZE 30         // 缓冲区30字节

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

uint64_t allowedDevices[] = {
  0x248B9C697090,  // 第0个设备  v4
  0x6809A21B5BF8,  // 第1个设备  v4
  0x8442AAAC85D8,  // 第1个设备  v3
  0x301BA21B5BF8,  // 第1个设备  v4
  0x0C46AAAC85D8,  // 第1个设备  v3
  0x9875555        // 第2个设备
};

String deviceName = "v3-x";

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
    deviceName = "v3-" + String(index);  // 拼接成 "id0", "id1" 等
    Serial.println("设备认证成功，设备名为: " + deviceName);
    // 5. (可选) 将这个名字设置为 WiFi 的主机名，方便在路由器后台查看
  } else {
    Serial.println("错误：该设备编号不在白名单中！");
    // 你可以在这里添加处理逻辑，比如让设备进入报错状态或停止运行
  }
}

void initRola() {


  // 绑定LoRa模块的事件回调函数
  RadioEvents.TxDone = OnTxDone;        // 发送完成
  RadioEvents.TxTimeout = OnTxTimeout;  // 发送超时
  RadioEvents.RxDone = OnRxDone;        // 接收完成

  Radio.Init(&RadioEvents);        // 初始化LoRa模块
  Radio.SetChannel(RF_FREQUENCY);  // 设置LoRa工作频率

  // 配置LoRa发送参数
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

  // 配置LoRa接收参数
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  state = STATE_TX;  // 初始状态：准备发送
}

/* ==================== 初始化函数（只执行一次） ==================== */
void setup() {
  Serial.begin(115200);                    // 初始化串口波特率115200
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);  // 初始化开发板硬件


  makeDivceName();
  initRola();
}

/* ==================== 定时打印变量 ==================== */
unsigned long lastPrintTime = 0;  // 记录上一次打印的时间
const long printInterval = 10000;  // 打印间隔：10秒（10000ms）
int skipNum = 0;

/* ==================== 主循环（反复执行） ==================== */
void loop() {
  openLedByNum(1, 500);  // LED闪烁1次，每次500ms（运行提示）

  // ============== 每10秒执行一次：打印系统状态 ==============
  if (millis() - lastPrintTime >= printInterval) {
    lastPrintTime = millis();  // 更新最后打印时间

    // 如果当前处于休眠状态，强制切换到发送状态
    if (state == LOWPOWER) {
      Radio.Sleep();  // LoRa模块休眠
      state = STATE_TX;
      Serial.println("【每20秒定时记录】系统运行中...");
    }
  }

  // ============== 根据当前状态执行不同操作 ==============
  switch (state) {

    // -------------------- 状态1：发送数据 --------------------
    case STATE_TX:
      delay(1000);  // 延时1秒再发送
      skipNum++;

      // 格式化发送内容：hello + 包号 + 收到的信号强度

      sprintf(txpacket, "1|%s|0.000000,0.000000|%d", deviceName,skipNum);

      // 串口打印发送信息
      Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

      Radio.Send((uint8_t *)txpacket, strlen(txpacket));  // 执行发送
      openLedByNum(10, 50);                               // 发送时快速闪烁LED10次

      state = LOWPOWER;  // 发送完成 → 进入休眠等待中断
      break;



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