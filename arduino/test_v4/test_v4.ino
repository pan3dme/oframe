/* Heltec Automation Receive communication test example
 *
 * Function:
 * 1. 只做一件事：接收同频段 LoRa 信号，打印 RSSI
 *    这是 V4 官方最干净、最准的测试代码
 * 
 * HelTec AutoMation, ChengDu, China
 * 成都惠利特自动化科技有限公司
 * */

#include "LoRaWan_APP.h"   // Heltec 官方 LoRa 驱动库（必须）
#include "Arduino.h"      // Arduino 基础库


// ======================== LoRa 核心参数配置 ========================
// 433MHz 频段（必须和发射端完全一样）
#define RF_FREQUENCY                                433000000 

// 发射功率（你是接收端，这个值不影响接收！）
#define TX_OUTPUT_POWER                             14        

// 带宽：0 = 125kHz（灵敏度最高，远距离首选）
#define LORA_BANDWIDTH                              0         
/*
带宽说明：
0 = 125kHz  灵敏度最高、距离最远
1 = 250kHz
2 = 500kHz
*/

// 扩频因子 SF10（距离 & 速度 平衡最佳）
#define LORA_SPREADING_FACTOR                       10        
// SF7~SF12：数字越大，距离越远，速度越慢

// 纠错率 1 = 4/5（最灵敏）
#define LORA_CODINGRATE                             1         
/*
1：4/5  最灵敏
2：4/6
3：4/7
4：4/8
*/

// 前导码长度 8（发射接收必须一致）
#define LORA_PREAMBLE_LENGTH                        8         

// 符号超时（0=一直监听，不自动退出）
#define LORA_SYMBOL_TIMEOUT                         0         

// 固定长度包关闭（用变长，通用）
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false

// IQ 反转关闭（标准模式）
#define LORA_IQ_INVERSION_ON                        false


// ======================== 接收相关参数 ========================
// 接收超时时间（这里没用，因为设了一直监听）
#define RX_TIMEOUT_VALUE                            1000

// 接收缓冲区大小（最大接收30字节）
#define BUFFER_SIZE                                 30 

// 发送包数组（接收端没用）
char txpacket[BUFFER_SIZE];

// 接收数据存储数组
char rxpacket[BUFFER_SIZE];

// 射频事件结构体（库内部用）
static RadioEvents_t RadioEvents;

// 发送计数（接收端没用）
int16_t txNumber;

// 信号强度 rssi + 数据长度
int16_t rssi,rxSize;

// 状态机：true=空闲，false=正在接收
bool lora_idle = true;


// ======================== 初始化 setup ========================
void setup() {
    Serial.begin(115200);          // 串口波特率
    Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);  // 初始化开发板硬件
    
    txNumber=0;     // 发送计数清零（没用）
    rssi=0;         // RSSI 初始值
  
    // 绑定接收完成回调函数：收到数据自动进入 OnRxDone()
    RadioEvents.RxDone = OnRxDone;

    // 初始化 LoRa 芯片硬件
    Radio.Init( &RadioEvents );

    // 设置工作频率：433MHz
    Radio.SetChannel( RF_FREQUENCY );

    // ======================== 最重要：接收配置 ========================
    Radio.SetRxConfig(
      MODEM_LORA,                // 模式：LoRa
      LORA_BANDWIDTH,            // 带宽 125k
      LORA_SPREADING_FACTOR,     // SF10
      LORA_CODINGRATE,           // 纠错 4/5
      0,                         
      LORA_PREAMBLE_LENGTH,      // 前导码 8
      LORA_SYMBOL_TIMEOUT,       // 0=一直监听
      LORA_FIX_LENGTH_PAYLOAD_ON,// 变长包
      0,
      true,                      // 开启 CRC 校验
      0,
      0,
      LORA_IQ_INVERSION_ON,      // IQ 正常模式
      true                       // 连续接收模式（V4官方标准）
    );
}


// ======================== 主循环 loop ========================
void loop()
{
  // 如果 LoRa 处于空闲状态 → 开启接收
  if(lora_idle)
  {
    lora_idle = false;        // 标记：正在接收
    Serial.println("into RX mode");
    Radio.Rx(0);              // 开启接收（0=永久监听，不超时）
  }

  Radio.IrqProcess( );  // 必须调用：处理 LoRa 中断（接收、发送事件）
}


// ======================== 接收完成自动回调 ========================
// 只要收到完整数据，自动进入这个函数
void OnRxDone(
  uint8_t *payload,   // 收到的数据内容
  uint16_t size,      // 数据长度
  int16_t rssi,       // 信号强度（你最关心的值）
  int8_t snr          // 信噪比
)
{
    rssi = rssi;                // 保存 RSSI
    rxSize = size;              // 保存数据长度
    memcpy(rxpacket, payload, size );  // 复制数据到数组
    rxpacket[size]='\0';        // 字符串结束符

    Radio.Sleep( );             // 接收完成 → 休眠（省电）

    // 串口打印结果
    Serial.printf("\r\n收到数据：%s，RSSI：%d，长度：%d\r\n", rxpacket, rssi, rxSize);

    lora_idle = true;  // 标记空闲 → loop 会重新开启接收
}