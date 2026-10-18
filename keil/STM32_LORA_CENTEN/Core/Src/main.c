/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 收发器主程序 - STM32F103C8T6
  *                   与 CenterReceiveBLE_v4 LoRa 配置匹配
  * @note           功能说明：
  *                 1. LoRa 通信：支持接收和发送两种模式，可通过按键 K1/K2 切换
  *                 2. LED 指示：LED1 接收状态指示（收到数据亮500ms）
  *                 3. 调试输出：通过 USART1 输出系统状态
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
// STM32 HAL 库主头文件，包含所有 HAL 驱动接口
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"        // 标准输入输出库，用于 printf 调试输出
#include "stdlib.h"       // 标准库函数，提供 atoi() 等工具函数
#include "string.h"       // 字符串处理库，提供 memcpy/strncmp/memset 等函数
#include "pan3029_rf.h"   // PAN3029 LoRa 射频驱动头文件
#include "pan3029_port.h" // PAN3029 硬件端口适配层头文件
#include "radio.h"        // LoRa 射频参数配置 (DEFAULT_FREQ/DEFAULT_SF 等)
#include "common_test.h"  // 公共测试模块（Common 目录共享）
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* DTU 发送队列：环形缓冲区，每条消息最大 256 字节，最多 5 条 */
#define DTU_QUEUE_SIZE     5
#define DTU_MSG_MAX_LEN    256

typedef struct {
  char   buf[DTU_MSG_MAX_LEN];  // 消息内容
  uint16_t len;                 // 消息长度
} DtuQueueItem_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LY-020 LoRa Gps"  // 工程名称
#define DEMO_VER     "V1.0"                  // 演示版本号
#define EXAMPLE_DATE "2026/10/04"            // 工程日期

#define RX_LEN       36                      // LoRa 接收缓冲区最大长度（字节）
#define LORA_TX_DELAY_MS  1500               // LoRa 接收后延时发送时间（ms）

/* Work mode - 工作模式定义 */
#define MODE_IDLE    0  // 空闲模式
#define MODE_RX      1  // 接收模式
#define MODE_TX      2  // 发送模式
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;
I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;              // SPI1 用于 LoRa PAN3029（硬件 SPI）
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;  // USART3 用于 DTU 通信



static char deviceName[16] = "v4-0";            // 设备名称（根据 UID 动态生成）

/* USER CODE BEGIN PV */
/* 设备 UID 列表（10 台设备），用于根据芯片 UID 确定设备编号 */
static const uint32_t deviceUidList[10][3] = {
  {0x003A002E, 0x46343030, 0x35343138},  // 设备 0
  {0x003A002E, 0x46343030, 0x35343139},  // 设备 1
  {0x003A002E, 0x46343030, 0x3534313A},  // 设备 2
  {0x003A002E, 0x46343030, 0x3534313B},  // 设备 3
  {0x003A002E, 0x46343030, 0x3534313C},  // 设备 4
  {0x003A002E, 0x46343030, 0x3534313D},  // 设备 5
  {0x003A002E, 0x46343030, 0x3534313E},  // 设备 6
  {0x003A002E, 0x46343030, 0x3534313F},  // 设备 7
  {0x67184122, 0x49867050, 0x066BFF49},  // 设备 8
  {0x67183650, 0x49867050, 0x0669FF49},  // 设备 9
 
};

// PB15 (LED1) 接收指示：收到LoRa数据时亮500ms
static uint32_t led1OnTick = 0;   // LED1 点亮时间戳
static uint8_t  led1On = 0;       // LED1 当前是否点亮（0=灭, 1=亮）

// 工作模式相关
static uint8_t  workMode = MODE_IDLE;  // 当前工作模式（RX/TX/IDLE）
static uint8_t  txMsg[] = "I LOVE YOU"; // TX模式下循环发送的测试消息
static uint32_t lastTxTick = 0;        // 上次发送时间戳（3秒间隔）
static uint32_t lastKeyTick = 0;       // 按键消抖时间戳（300ms消抖间隔）

/* LoRa receive buffers - LoRa 接收缓冲区 */
static uint8_t  prev_rx_buf[RX_LEN] = {0};  // 上次接收到的数据（用于对比是否重复）
static uint8_t  prev_rx_size = 0;           // 上次接收数据的长度

extern struct RxDoneMsg RxDoneParams;  // 外部声明：接收完成时的消息结构体（在驱动中定义）

/* DTU (USART3) 接收缓冲区 */
#define DTU_RX_BUF_SIZE  256
static uint8_t  dtuRxBuf[DTU_RX_BUF_SIZE];  // DTU 接收缓冲区
static uint16_t dtuRxLen = 0;               // 当前接收到的数据长度
static uint32_t dtuLastRxTick = 0;          // 上次收到字节的时间戳（用于超时判断）
static uint32_t dtuCsqTick = 0;             // nettime 查询定时器：记录上次发送时间
static char dtuNetTimeStr[32] = "0";        // DTU 网络时间戳（秒），用于上报
static uint32_t dtuTimestamp = 0;           // DTU 网络时间 Unix 时间戳（秒）
static uint32_t dtuSyncTick = 0;            // 对时时的 HAL_GetTick() 值，用于实时计算
static uint32_t lastPrintTick = 0;          // [调试] 每秒打印时间用
static uint8_t dtuTimeValid = 0;            // 网络时间是否已获取成功
static int dtuCsqRssi = 0;                  // DTU CSQ 信号强度值
static uint32_t rtcSendCount = 0;           // 对时上报计数器
static int batteryValue = 85;               // 电池电量（%）
static char configStr[32] = "30,0M";          // 配置信息字符串（可通过DTU修改）
static uint8_t configBootSent = 0;           // 开机配置是否已上报（0=未上报, 1=已上报）

/* DTU 发送队列变量 */
static DtuQueueItem_t dtuQueue[DTU_QUEUE_SIZE];
static uint8_t dtuQueueHead = 0;   // 出队索引
static uint8_t dtuQueueTail = 0;   // 入队索引
static uint8_t dtuQueueCount = 0;  // 当前队列中的消息数量
static uint32_t dtuLastSendTick = 0;  // 上次发送时间戳（用于 100ms 间隔控制）

/* Flash 配置存储 */
#define CONFIG_FLASH_ADDR  0x0800FC00  // Flash 最后一页（STM32F103C8T6 64KB）
#define CONFIG_MAGIC       0xA5A5A5A5  // 有效数据标志

/* 待执行指令缓存：每个设备最多缓存一条 DTU 下发的指令 */
#define PENDING_CMD_MAX_SLOTS  8   // 最多缓存 8 个设备的待执行指令

typedef struct {
  uint8_t used;                  // 该槽位是否被占用
  char    deviceId[32];          // 目标设备 ID
  char    cmd[32];               // 指令内容
  char    val[32];               // 指令参数
} PendingCmd_t;

static PendingCmd_t pendingCmdCache[PENDING_CMD_MAX_SLOTS];

/* gpstm 中继指令状态：收到 gpstm 后在有效期内下发 upgps 指令 */
static uint8_t  gpstmActive = 0;              // gpstm 中继是否激活（0=未激活, 1=激活）
static uint32_t gpstmRecvTick = 0;            // 收到 gpstm 时的 HAL_GetTick() 值
static uint32_t gpstmTimeoutMin = 0;          // gpstm 有效时长（分钟），来自 value 字段

/* USER CODE END PV */
/* Private function prototypes -----------------------------------------------*/
// 系统函数声明
void SystemClock_Config(void);       // 系统时钟配置函数
static void MX_GPIO_Init(void);      // GPIO 初始化函数
static void MX_SPI1_Init(void);       // SPI1 初始化（LoRa 硬件 SPI）
static void MX_USART1_UART_Init(void); // USART1 初始化（调试串口）
static void MX_USART3_UART_Init(void); // USART3 初始化（DTU 通信）
static void MX_NVIC_Init(void);      // NVIC 中断控制器初始化

/* USER CODE BEGIN PFP */
// 用户自定义函数声明
static void printf_logo(void);       // 打印工程信息 Logo
static int  getDeviceIndex(void);    // 根据芯片 UID 获取设备编号
static void OnSlave(void);           // LoRa 从机接收处理函数
static void enter_rx_mode(void);     // 切换到接收模式
static void do_tx_send(void);        // 发送模式处理函数
static void sendToDtu(const char *loraStr, int rssi, int snr);  // 统一上报DTU（JSON格式，入队）
static void enqueueDtuMsg(const char *msg, int len);  // 将消息加入 DTU 发送队列
static void processDtuQueue(void);  // 处理 DTU 发送队列（每秒发一条）
static void loadConfigFromFlash(void);      // 从 Flash 加载配置
static void saveConfigToFlash(const char *str);  // 保存配置到 Flash
static void parseDtuCommand(char *buf);      // JSON 解析 DTU 下发指令
static uint32_t getCurrentTimestamp(void);   // 获取当前实时时间戳
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour, int min, int sec);  // 日期转Unix时间戳
static uint32_t getConfigIntervalMs(void);   // 从 configStr 解析对时间隔（毫秒）
static void cachePendingCommand(const char *deviceId, const char *cmd, const char *val);  // 缓存待执行指令
static uint8_t checkPendingCommand(const char *deviceId);  // 检查是否有缓存指令，返回1=有,0=无
static uint8_t sendPendingCommand(const char *deviceId);   // 发送缓存指令并清除，返回1=已发送,0=无

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  根据芯片 UID 获取设备编号
  * @note   读取芯片 96 位 UID，在 deviceUidList 中查找匹配项
  * @retval 设备编号 (0-9)，未找到返回 -1
  */
static int getDeviceIndex(void)
{
  uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7E8;
  uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7EC;
  uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7F0;

  for (int i = 0; i < 10; i++)
  {
    if (deviceUidList[i][0] == uid2 &&
        deviceUidList[i][1] == uid1 &&
        deviceUidList[i][2] == uid0)
    {
      return i;
    }
  }
  return -1;  // 未找到匹配的设备
}


#ifdef __GNUC__
  // GCC 编译器使用 __io_putchar 实现 printf 重定向
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  // Keil ARMCC 编译器使用 fputc 实现 printf 重定向
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
  * @brief  printf 底层输出函数重定向
  * @note   将 printf 输出的每个字符通过 USART1 发送出去
  * @param  ch: 要输出的字符
  * @retval 返回输出的字符
  */
PUTCHAR_PROTOTYPE
{
  // 通过 USART1 发送单个字符，超时时间 0xFFFF
  HAL_UART_Transmit(&huart1,(uint8_t *)&ch,1,0xFFFF);
  return ch;
}

/**
  * @brief  打印工程信息 Logo
  * @note   在系统启动时调用，显示工程名称、版本、日期、硬件配置等信息
  */
static void printf_logo(void)
{
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* Project Name  : %s\r\n", EXAMPLE_NAME);    // 工程名称
  printf("* Demo Version  : %s\r\n", DEMO_VER);        // 版本号
  printf("* Date          : %s\r\n", EXAMPLE_DATE);    // 日期
  printf("* MCU           : STM32F103C8T6\r\n");       // 主控芯片
  printf("* Radio         : PAN3029 (LoRa)\r\n");      // 射频模块
  printf("*************************************************************\r\n");
}

/**
  * @brief  LoRa 从机接收处理函数
  * @note   功能说明：
  *         1. 检查接收完成标志，若收到数据则读取 RSSI/SNR/数据内容
  *         2. 与上次接收的数据对比，标记为 [SAME] 或 [DIFF]
  *         3. 打印接收信息（信号强度、信噪比、长度、内容）
  *         4. 处理接收错误（CRC 错误、超时）
  *         5. 重新进入单次超时接收模式（超时时间 5-15 秒）
  */
static void OnSlave(void)
{
  // 检查是否接收完成
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
  {
    uint8_t local_buf[RX_LEN];    // 本地接收缓冲区
    uint8_t local_size;           // 接收数据长度
    int8_t  local_rssi;           // 接收信号强度指示 (dBm)
    float   local_snr;            // 信噪比 (dB)
    uint8_t is_same = 0;          // 数据重复标志

    // 临界区保护：关中断，快速读取接收数据
    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);           // 清除接收完成标志
    local_rssi = RxDoneParams.Rssi;              // 读取 RSSI
    local_snr  = RxDoneParams.Snr;               // 读取 SNR
    local_size = RxDoneParams.Size;              // 读取数据长度
    if (local_size > RX_LEN) local_size = RX_LEN; // 防止缓冲区溢出
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];    // 拷贝数据到本地缓冲区
    __enable_irq();  // 开中断

    /* 与上次接收的数据对比，判断是否重复 */
    if (local_size == prev_rx_size && prev_rx_size > 0)
    {
      is_same = 1;  // 假设相同
      for (uint8_t i = 0; i < local_size; i++)
      {
        if (local_buf[i] != prev_rx_buf[i])
        {
          is_same = 0;  // 发现不同，标记为不同
          break;
        }
      }
    }

    // 打印接收信息
    printf("Rssi: %d  ", local_rssi - 256);  // RSSI 值（需减去 256 得到实际 dBm）
    printf("Snr: %d   ", (int)local_snr);    // SNR 值
    printf("Len: %d  ", local_size);         // 数据长度
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++)
    {
      if (local_buf[i] == 0) break;  // 遇到字符串结束符停止
      printf("%c", local_buf[i]);    // 打印字符
    }
    if (is_same)
      printf("  [SAME]");  // 标记为重复数据
    else
      printf("  [DIFF]");  // 标记为新数据
    printf("\r\n");

    /* 保存当前数据作为上次数据，用于下次对比 */
    prev_rx_size = local_size;
    for (uint8_t i = 0; i < local_size; i++)
      prev_rx_buf[i] = local_buf[i];

    /* PB15 (LED1) 点亮，指示收到LoRa数据，500ms后由主循环熄灭 */
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    led1OnTick = HAL_GetTick();
    led1On = 1;

    /* 解析消息类型并处理上报 */
    if (local_size < RX_LEN) local_buf[local_size] = '\0';
    int msgType = atoi((char *)local_buf);
    printf("[LoRa] MsgType: %d\r\n", msgType);

    if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_TIME)
    {
      /* 从 LoRa 数据中提取 deviceId（第二个 | 分隔字段），检查是否有缓存的待执行指令 */
      uint8_t hasPendingCmd = 0;
      char loraDevId[32] = {0};
      const char *p1 = strchr((char *)local_buf, '|');
      if (p1)
      {
        const char *p2 = p1 + 1;
        const char *p3 = strchr(p2, '|');
        int idLen = p3 ? (int)(p3 - p2) : (int)strlen(p2);
        if (idLen > 31) idLen = 31;
        memcpy(loraDevId, p2, idLen);
        loraDevId[idLen] = '\0';
        hasPendingCmd = checkPendingCommand(loraDevId);
      }

      char reportBuf[RX_LEN];

      if (msgType == MSG_TYPE_TIME)
      {
        // 调用 common 函数替换时间字段（仅用于上报 DTU）
        int ret = common_replaceTimeSegment((char *)local_buf, reportBuf, sizeof(reportBuf),
                                            getCurrentTimestamp(), NULL);
        if (ret == 0)
        {
          printf("[LoRa 接收] : %s\r\n", reportBuf);
          sendToDtu(reportBuf, local_rssi - 256, (int)local_snr);
        }
        else
        {
          sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
        }
      }
      else
      {
        // MSG_TYPE_GPS: 直接上报原始数据
        sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
      }

      /* 统一延时后下发：有缓存指令则下发指令，否则检查 gpstm 中继或对时 */
      HAL_Delay(LORA_TX_DELAY_MS);

      if (hasPendingCmd)
      {
        sendPendingCommand(loraDevId);
      }
      else if (gpstmActive && (HAL_GetTick() - gpstmRecvTick) < gpstmTimeoutMin * 60 * 1000)
      {
        /* gpstm 中继有效：在有效期内，任意设备上报时下发 upgps 指令 */
        char txData[64];
        int txLen = snprintf(txData, sizeof(txData), "%d|%s|upgps|0",
                             MSG_TYPE_COM, loraDevId);
        uint32_t tx_time = 0;
        rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
        printf("[LoRa gpstm 中继] %s\r\n", txData);

        /* 等待发送完成 */
        uint32_t waitStart = HAL_GetTick();
        while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE)
        {
          if (HAL_GetTick() - waitStart > 3000) break;
        }
        rf_set_transmit_flag(RADIO_FLAG_IDLE);
      }
      else
      {
        // 构造下发数据: 4|deviceName|timestamp| （使用 STM32 实时时间戳）
        char txData[64];
        int txLen = snprintf(txData, sizeof(txData), "%d|%lu|%s",
                             MSG_TYPE_SYN_TIME, (unsigned long)getCurrentTimestamp(), deviceName);

        // 发送 LoRa 数据
        uint32_t tx_time = 0;
        rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
        printf("[LoRa 下发对时] %s\r\n", txData);

        // 等待发送完成
        uint32_t waitStart = HAL_GetTick();
        while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE)
        {
          if (HAL_GetTick() - waitStart > 3000) break;  // 3秒超时保护
        }
        rf_set_transmit_flag(RADIO_FLAG_IDLE);
      }
    }

    /* MSG_TYPE_CONFIG: 直接上报原始数据，不对时 */
    if (msgType == MSG_TYPE_CONFIG)
    {
      sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
    }

    rf_enter_single_timeout_rx(15000);  // 重新进入接收模式，超时 15 秒
  }

  // 处理 CRC 校验错误
  if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
  {
    printf("crc error\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);  // 重新进入接收模式，超时 5 秒
  }

  // 处理接收超时
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
  {
    printf(".");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);  // 重新进入接收模式，超时 5 秒
  }
}

/**
  * @brief  切换到接收模式
  * @note   功能说明：
  *         1. 设置工作模式为 MODE_RX
  *         2. 关闭 LED1（接收模式下 LED1 以 500ms 间隔闪烁）
  *         3. 打印模式切换信息
  *         4. 进入单次超时接收模式，超时时间 5 秒
  */
static void enter_rx_mode(void)
{
    workMode = MODE_RX;  // 设置工作模式为接收
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);  // 关闭 LED1
    led1On = 0;  // 同步 LED1 状态变量
    printf("\r\n>> Switch to RX mode\r\n");
    rf_enter_single_timeout_rx(5000);  // 进入接收模式，超时 5 秒
}

/**
  * @brief  发送模式处理函数
  * @note   功能说明：
  *         1. 每 3 秒发送一次测试消息 "I LOVE YOU"
  *         2. 发送时 LED1 点亮 500ms 作为发送指示
  *         3. 清除发送完成标志，准备下次发送
  */
static void do_tx_send(void)
{
    uint32_t now = HAL_GetTick();  // 获取当前系统时间

    /* 每 3 秒发送一次数据 */
    if (now - lastTxTick >= 3000)
    {
        lastTxTick = now;  // 更新发送时间戳
        uint32_t tx_time;
        // 调用驱动发送数据，tx_time 返回实际发送耗时
        rf_single_tx_data(txMsg, sizeof(txMsg) - 1, &tx_time);
        printf("TX: I LOVE YOU\r\n");
    }

    /* 检查并清除发送完成标志 */
    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        rf_set_transmit_flag(RADIO_FLAG_IDLE);  // 清除标志，准备下次发送
    }
}

/**
  * @brief  将日期时间转换为 Unix 时间戳（秒）
  * @note   基于 1970-01-01 00:00:00 UTC，不考虑时区
  */
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour, int min, int sec)
{
  static const int daysBeforeMonth[] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
  uint32_t days, timestamp;
  int y;

  // 计算从 1970-01-01 到指定日期的天数
  y = year - 1;
  days = (uint32_t)(year - 1970) * 365
       + (y/4 - y/100 + y/400) - (1969/4 - 1969/100 + 1969/400)
       + daysBeforeMonth[month] + day - 1;
  if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days++;
  timestamp = days * 86400UL + (uint32_t)hour * 3600 + (uint32_t)min * 60 + (uint32_t)sec;
  return timestamp;
}

/**
  * @brief  从 configStr 解析对时间隔（毫秒）
  * @note   configStr 格式如 "30,0M"，取逗号前的数字作为分钟数
  *         限制范围：5~60 分钟，超出则按边界值处理
  * @retval 间隔时间（毫秒）
  */
static uint32_t getConfigIntervalMs(void)
{
  int minutes = atoi(configStr);
  if (minutes < 5)  minutes = 5;
  if (minutes > 60) minutes = 60;
  return (uint32_t)minutes * 60 * 1000;
}

/**
  * @brief  获取当前实时时间戳（基于网络对时 + HAL 计时）
  * @note   对时后以 dtuTimestamp 为基准，加上经过的秒数
  */
static uint32_t getCurrentTimestamp(void)
{
  if (!dtuTimeValid) return 0;
  return dtuTimestamp + (HAL_GetTick() - dtuSyncTick) / 1000;
}

/**
  * @brief  从 Flash 加载配置信息
  * @note   开机时调用，如果 Flash 中有有效配置则加载到 configStr
  */
static void loadConfigFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)CONFIG_FLASH_ADDR;
  if (magic == CONFIG_MAGIC)
  {
    const char *p = (const char *)(CONFIG_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(configStr) - 1 && p[i] != '\0'; i++)
      configStr[i] = p[i];
    configStr[i] = '\0';
    printf("[Flash] 加载配置: %s\r\n", configStr);
  }
  else
  {
    printf("[Flash] 无有效配置，使用默认: %s\r\n", configStr);
  }
}

/**
  * @brief  保存配置信息到 Flash
  * @param  str: 要保存的配置字符串
  */
static void saveConfigToFlash(const char *str)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;
  uint8_t i;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = CONFIG_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  // 写入魔术字
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_FLASH_ADDR, CONFIG_MAGIC);

  // 写入配置字符串（按半字写入）
  for (i = 0; i < strlen(str) + 1; i += 2)
  {
    uint16_t halfWord = str[i] | (str[i + 1] << 8);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, CONFIG_FLASH_ADDR + 4 + i, halfWord);
  }

  HAL_FLASH_Lock();
  printf("[Flash] Config saved: %s\r\n", str);
}

/**
  * @brief  将消息加入 DTU 发送队列（环形缓冲区）
  * @param  msg: 消息数据指针
  * @param  len: 消息长度
  * @note   队列满时丢弃最早的消息（覆盖队头）
  */
static void enqueueDtuMsg(const char *msg, int len)
{
  if (len <= 0 || len >= DTU_MSG_MAX_LEN) return;

  __disable_irq();
  if (dtuQueueCount >= DTU_QUEUE_SIZE)
  {
    // 队列满，丢弃最早的一条（队头前移）
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
    printf("[DTU Queue] 队列满，丢弃旧消息\r\n");
  }
  // 拷贝消息到队尾
  memcpy(dtuQueue[dtuQueueTail].buf, msg, len);
  dtuQueue[dtuQueueTail].buf[len] = '\0';
  dtuQueue[dtuQueueTail].len = (uint16_t)len;
  dtuQueueTail = (dtuQueueTail + 1) % DTU_QUEUE_SIZE;
  dtuQueueCount++;
  __enable_irq();
}

/**
  * @brief  处理 DTU 发送队列：每 1 秒从队头取出一条消息发送
  * @note   在主循环中调用，非阻塞
  */
static void processDtuQueue(void)
{
  if (dtuQueueCount == 0) return;

  if (HAL_GetTick() - dtuLastSendTick >= 500)
  {
    dtuLastSendTick = HAL_GetTick();

    // 从队头取出一条消息
    DtuQueueItem_t *item = &dtuQueue[dtuQueueHead];
    HAL_UART_Transmit(&huart3, (uint8_t *)item->buf, item->len, 1000);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 100);
    printf("[DTU 队列发送] %s\r\n", item->buf);

    // 出队
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
  }
}

/**
  * @brief  统一上报数据给 DTU（JSON格式）
  * @note   数据先入队，由 processDtuQueue() 每秒发送一条
  * @param  loraStr: lorainfo 字段内容
  * @param  rssi:    RSSI 信号强度
  * @param  snr:     SNR 信噪比
  */
static void sendToDtu(const char *loraStr, int rssi, int snr)
{
  char txBuf[DTU_MSG_MAX_LEN];
  int len = snprintf(txBuf, sizeof(txBuf),
      "{"
      "\"version\":\"1.0\","
      "\"method\":\"thing.event.property.post\","
      "\"params\":{"
      "\"lorainfo\":\"%s\","
      "\"upDateDevice\":\"%s\","
      "\"tm\":%lu,"
      "\"rssi\":%d,"
      "\"snr\":%d"
      "}"
      "}",
      loraStr, (char *)deviceName, (unsigned long)getCurrentTimestamp(), rssi, snr);

  if (len > 0 && len < (int)sizeof(txBuf))
  {
    enqueueDtuMsg(txBuf, len);
    printf("[DTU 入队] 队列=%d/%d\r\n", dtuQueueCount, DTU_QUEUE_SIZE);
  }
}

/**
  * @brief  获取到网络时间后，上报一次对时信息给 DTU
  * @note   数据格式: 2|deviceName|timestamp|battery|count
  */
static void sendTimeSyncToDtu(void)
{
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%lu|%d|%lu",
           MSG_TYPE_TIME, deviceName,
           (unsigned long)getCurrentTimestamp(),
           batteryValue,
           (unsigned long)rtcSendCount++);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/**
  * @brief  开机网络正常后，上报一次配置信息给 DTU
  * @note   数据格式: 6|deviceName|config_str|count
  */
static void sendConfigToDtu(void)
{
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|%lu",
           MSG_TYPE_CONFIG, deviceName,
           configStr,
           (unsigned long)rtcSendCount++);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/**
  * @brief  缓存待执行指令（每个设备最多一条，新指令替换旧指令）
  * @param  deviceId: 目标设备 ID
  * @param  cmd:      指令内容
  * @param  val:      指令参数
  */
static void cachePendingCommand(const char *deviceId, const char *cmd, const char *val)
{
  int freeSlot = -1;

  /* 先查找是否已有该设备的缓存槽位 */
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++)
  {
    if (pendingCmdCache[i].used && strcmp(pendingCmdCache[i].deviceId, deviceId) == 0)
    {
      /* 已有该设备的缓存，直接替换 */
      strncpy(pendingCmdCache[i].cmd, cmd, sizeof(pendingCmdCache[i].cmd) - 1);
      pendingCmdCache[i].cmd[sizeof(pendingCmdCache[i].cmd) - 1] = '\0';
      strncpy(pendingCmdCache[i].val, val, sizeof(pendingCmdCache[i].val) - 1);
      pendingCmdCache[i].val[sizeof(pendingCmdCache[i].val) - 1] = '\0';
      printf("[PENDING CMD] Replaced: device=%s, cmd=%s, val=%s\r\n", deviceId, cmd, val);
      return;
    }
    if (!pendingCmdCache[i].used && freeSlot < 0)
      freeSlot = i;
  }

  /* 未找到已有槽位，使用空闲槽位 */
  if (freeSlot >= 0)
  {
    pendingCmdCache[freeSlot].used = 1;
    strncpy(pendingCmdCache[freeSlot].deviceId, deviceId, sizeof(pendingCmdCache[freeSlot].deviceId) - 1);
    pendingCmdCache[freeSlot].deviceId[sizeof(pendingCmdCache[freeSlot].deviceId) - 1] = '\0';
    strncpy(pendingCmdCache[freeSlot].cmd, cmd, sizeof(pendingCmdCache[freeSlot].cmd) - 1);
    pendingCmdCache[freeSlot].cmd[sizeof(pendingCmdCache[freeSlot].cmd) - 1] = '\0';
    strncpy(pendingCmdCache[freeSlot].val, val, sizeof(pendingCmdCache[freeSlot].val) - 1);
    pendingCmdCache[freeSlot].val[sizeof(pendingCmdCache[freeSlot].val) - 1] = '\0';
    printf("[PENDING CMD] Cached: device=%s, cmd=%s, val=%s\r\n", deviceId, cmd, val);
  }
  else
  {
    printf("[PENDING CMD] Cache full! Cannot cache cmd for device=%s\r\n", deviceId);
  }
}

/**
  * @brief  检查并处理指定设备的待执行缓存指令
  * @note   当 LoRa 收到该设备的上报数据时调用，打印缓存指令并清除缓存
  * @param  deviceId: 上报数据的设备 ID
  */
static uint8_t checkPendingCommand(const char *deviceId)
{
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++)
  {
    if (pendingCmdCache[i].used && strcmp(pendingCmdCache[i].deviceId, deviceId) == 0)
    {
      printf("[PENDING CMD] Found pending cmd for device=%s: cmd=%s, val=%s\r\n",
             deviceId, pendingCmdCache[i].cmd, pendingCmdCache[i].val);
      return 1;  /* 有缓存指令 */
    }
  }
  return 0;  /* 无缓存指令 */
}

/**
  * @brief  通过 LoRa 发送缓存指令给目标设备并清除缓存
  * @param  deviceId: 目标设备 ID
  * @retval 1=已发送, 0=无缓存
  */
static uint8_t sendPendingCommand(const char *deviceId)
{
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++)
  {
    if (pendingCmdCache[i].used && strcmp(pendingCmdCache[i].deviceId, deviceId) == 0)
    {
      /* 通过 LoRa 下发指令给目标设备，格式: 9|deviceId|cmd|val| */
      char txData[64];
      int txLen = snprintf(txData, sizeof(txData), "%d|%s|%s|%s",
                           MSG_TYPE_COM, pendingCmdCache[i].deviceId,
                           pendingCmdCache[i].cmd, pendingCmdCache[i].val);
      uint32_t tx_time = 0;
      rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
      printf("[PENDING CMD] LoRa TX: %s\r\n", txData);

      /* 等待发送完成 */
      uint32_t waitStart = HAL_GetTick();
      while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE)
      {
        if (HAL_GetTick() - waitStart > 3000) break;
      }
      rf_set_transmit_flag(RADIO_FLAG_IDLE);

      /* 清除该缓存槽位 */
      pendingCmdCache[i].used = 0;
      memset(&pendingCmdCache[i], 0, sizeof(PendingCmd_t));
      printf("[PENDING CMD] Cache cleared for device=%s, cmd delivered\r\n", deviceId);
      return 1;
    }
  }
  return 0;
}

/**
  * @brief  JSON 解析 DTU 下发的指令
  * @note   从外层 JSON 提取 cominfo 字段，反转义后解析内部 JSON
  *         内部格式: {"cmd":"xxx","value":"xxx","deviceId":"xxx"}
  */
static void parseDtuCommand(char *buf)
{
  /* Step 1: 从外层 JSON 提取 cominfo 字段值（处理转义） */
  char *pCominfo = strstr(buf, "\"cominfo\":\"");
  if (!pCominfo)
  {
    printf("[DTU JSON] cominfo not found\r\n");
    return;
  }

  pCominfo += 11;  // 跳过 "cominfo":"
  char unescaped[200];
  int ui = 0;

  // 读取 cominfo 字符串值，处理转义序列
  while (*pCominfo && ui < (int)sizeof(unescaped) - 1)
  {
    if (*pCominfo == '\\' && *(pCominfo + 1) != '\0')
    {
      char next = *(pCominfo + 1);
      if (next == '"' || next == '\\' || next == '/')
      {
        unescaped[ui++] = next;
        pCominfo += 2;
        continue;
      }
    }
    else if (*pCominfo == '"')
    {
      break;  // 未转义的引号 = cominfo 字符串结束
    }
    unescaped[ui++] = *pCominfo++;
  }
  unescaped[ui] = '\0';
  printf("[DTU JSON] cominfo: %s\r\n", unescaped);

  /* Step 2: 从反转义后的 JSON 提取各字段 */
  char cmd[32] = {0}, devId[32] = {0}, val[32] = {0};

  // 提取 cmd
  char *p = strstr(unescaped, "\"cmd\":");
  if (p)
  {
    p += 6;
    while (*p == ' ' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < 31) cmd[i++] = *p++;
    cmd[i] = '\0';
  }

  // 提取 deviceId
  p = strstr(unescaped, "\"deviceId\":");
  if (p)
  {
    p += 11;
    while (*p == ' ' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < 31) devId[i++] = *p++;
    devId[i] = '\0';
  }

  // 提取 value
  p = strstr(unescaped, "\"value\":");
  if (p)
  {
    p += 8;
    while (*p == ' ' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && i < 31) val[i++] = *p++;
    val[i] = '\0';
    // 去除 value 尾部多余的逗号
    int vlen = (int)strlen(val);
    while (vlen > 0 && val[vlen - 1] == ',') val[--vlen] = '\0';
  }

  printf("[DTU JSON] cmd=%s, deviceId=%s, value=%s\r\n", cmd, devId, val);

  /* Step 3: 检查 deviceId 是否为本机 */
  if (strcmp(devId, (char *)deviceName) != 0)
  {
    printf("[DTU CMD] Not this device (target=%s, local=%s), cache it\r\n", devId, deviceName);
    cachePendingCommand(devId, cmd, val);
    return;
  }

  /* Step 4: 执行指令 */
  if (strcmp(cmd, "relayreboot") == 0)
  {
    printf("[DTU CMD] 执行设备重启...\r\n");
    HAL_Delay(500);
    NVIC_SystemReset();
  }
  else if (strcmp(cmd, "A") == 0)
  {
    if (val[0] != '\0')
    {
      strncpy(configStr, val, sizeof(configStr) - 1);
      configStr[sizeof(configStr) - 1] = '\0';
      saveConfigToFlash(configStr);
      printf("[DTU CMD] Config updated: %s\r\n", configStr);
      HAL_Delay(1000);
      sendConfigToDtu();
      printf("[DTU CMD] 配置已上报\r\n");
    }
    else
    {
      printf("[DTU CMD] value 为空\r\n");
    }
  }
  else if (strcmp(cmd, "gpstm") == 0)
  {
    /* gpstm 中继指令：仅当 deviceId 匹配本机时激活，记录当前时间和有效时长 */
    if (strcmp(devId, (char *)deviceName) != 0)
    {
      printf("[DTU CMD] gpstm 目标设备不匹配(目标=%s, 本机=%s)，忽略\r\n", devId, deviceName);
      return;
    }
    gpstmActive = 1;
    gpstmRecvTick = HAL_GetTick();
    gpstmTimeoutMin = (uint32_t)atoi(val);
    if (gpstmTimeoutMin == 0) gpstmTimeoutMin = 30;  // 默认 30 分钟
    printf("[DTU CMD] gpstm 中继已记录: device=%s, timeout=%lu min\r\n",
           deviceName, (unsigned long)gpstmTimeoutMin);
  }
  else
  {
    printf("[DTU CMD] 未知指令: %s\r\n", cmd);
  }
}

/* USER CODE END 0 */

/**
  * @brief  应用程序主入口函数
  * @retval int (实际不会返回)
  * @note   主函数执行流程：
  *         1. HAL 库初始化（复位所有外设、初始化 Flash 和 SysTick）
  *         2. 系统时钟配置（72MHz）
  *         3. 外设初始化（GPIO、USART1、USART2、NVIC）
  *         4. LoRa 模块初始化和参数配置
  *         5. 默认进入接收模式
  *         5. 主循环：LED 心跳、模式处理
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/

  /* 复位所有外设，初始化 Flash 接口和 SysTick 定时器 */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 配置系统时钟 */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有配置的外设 */
  MX_GPIO_Init();           // GPIO 初始化（LED、按键、LoRa CSN/RST/IRQ）
  MX_SPI1_Init();           // SPI1 初始化（LoRa 硬件 SPI）
  MX_USART1_UART_Init();    // USART1 初始化（调试串口，115200 波特率）
  MX_USART3_UART_Init();    // USART3 初始化（DTU 通信，115200 波特率）

  /* 初始化 NVIC 中断控制器（LoRa IRQ 使用 PA2 EXTI） */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */

  // 打印启动信息
  printf("\r\n===== LoRa Receiver Start =====\r\n");
  printf("DTU UART (USART3): PB10=TX, PB11=RX, 115200 8N1\r\n");
  printf("USART3 CR1=0x%04X CR2=0x%04X CR3=0x%04X BRR=0x%04X SR=0x%04X\r\n",
         (uint16_t)huart3.Instance->CR1, (uint16_t)huart3.Instance->CR2,
         (uint16_t)huart3.Instance->CR3, (uint16_t)huart3.Instance->BRR,
         (uint16_t)huart3.Instance->SR);
  printf_logo();  // 显示工程信息 Logo

  /* 打印芯片唯一 ID 并根据 UID 生成 deviceName */
  {
    uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7E8;
    uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7EC;
    uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7F0;
    printf("Chip UID    : %08X-%08X-%08X\r\n", (unsigned int)uid2, (unsigned int)uid1, (unsigned int)uid0);

    int devIndex = getDeviceIndex();
    if (devIndex >= 0)
    {
      snprintf(deviceName, sizeof(deviceName), "v4-%d", 100 + devIndex);
      printf("Device Name : %s (index=%d)\r\n", deviceName, 100 + devIndex);
    }
    else
    {
      printf("Device Name : %s (UID not in list, use default)\r\n", deviceName);
    }
  }

  /* 从 Flash 加载配置 */
  loadConfigFromFlash();

  /* 测试公共模块 */
  commonTest_PrintInfo();
  printf("commonTest_Increment = %lu\r\n", (unsigned long)commonTest_Increment());

  HAL_Delay(1);  // 短暂延时，等待串口输出完成

  /* LoRa PAN3029 模块初始化 */
  printf("LoRa init...\r\n");

  /* 确保 CSN 和 RST 引脚为高电平（LoRa 模块空闲状态） */
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);  // CSN 拉高
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);  // RST 拉高
  HAL_Delay(100);  // 等待模块电源稳定

  // 初始化 LoRa 射频模块
  if (rf_init() != RF_OK)
  {
    // 初始化失败，进入错误循环
    printf("RF init fail! Check wiring.\r\n");
    while (1)
    {
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);  // LED1 快速闪烁表示错误
      HAL_Delay(100);
    }
  }
  printf("RF init ok\r\n");

  /* 设置 LoRa 默认参数（与 CenterReceiveBLE_v4 ETSI_868 配置一致）：
   *   频率 = 915 MHz, 扩频因子 SF = 10, 带宽 BW = 125 kHz
   *   编码率 CR = 4/5, 发射功率 = 22 dBm
   * 此函数还会打印: FREQ= xxx  SF=x   BW=x  CR=x */
  rf_set_default_para();

  // 打印 LoRa 配置信息（读取 radio.h 实际配置）
  printf("===== LoRa Ready =====\r\n");
  printf("  Freq    : %d MHz\r\n", DEFAULT_FREQ / 1000000);
  printf("  SF      : %d\r\n", DEFAULT_SF);
  printf("  BW      : 125 kHz\r\n");
  printf("  CR      : 4/5\r\n");
  printf("  Power   : %d dBm\r\n", DEFAULT_PWR);
  printf("  CRC     : OFF\r\n");
  printf("  SyncWord: 0x12\r\n");
  printf("----------------------------\r\n");
  printf("  K1 -> RX mode (LED 500ms blink)\r\n");  // 按键 K1 功能说明
  printf("  K2 -> TX mode (send 'I LOVE YOU' / 3s)\r\n");  // 按键 K2 功能说明
  printf("----------------------------\r\n");

  enter_rx_mode();  // 默认进入接收模式

  /* 记录启动时间，用于 5 秒后向 DTU 发送 CSQ 查询 */
  dtuCsqTick = HAL_GetTick();

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* [调试] 每秒打印一次 STM32 实时时间 */
    if (dtuTimeValid && HAL_GetTick() - lastPrintTick >= 1000)
    {
      lastPrintTick = HAL_GetTick();
      char timeBuf[24];
      common_formatTimestamp(getCurrentTimestamp(), timeBuf, sizeof(timeBuf));
      // printf("[TIME] %s\r\n", timeBuf);
    }

    /* PB15 (LED1) 接收指示：点亮500ms后自动熄灭 */
    if (led1On && HAL_GetTick() - led1OnTick >= 500)
    {
      led1On = 0;
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }

    /* DTU 发送队列处理：每秒从队列取出一条消息发送 */
    processDtuQueue();

    /* 根据当前工作模式执行相应处理 */
    switch (workMode)
    {
      case MODE_RX:  // 接收模式
        OnSlave();  // 调用接收处理函数
        break;

      case MODE_TX:  // 发送模式
        do_tx_send();  // 调用发送处理函数
        break;

      default:  // 空闲模式，不做任何处理
        break;
    }


    /* DTU 一帧完成判断：收到数据后 500ms 无新字节 或 缓冲区满 */
    if ((dtuRxLen > 0 && HAL_GetTick() - dtuLastRxTick > 500) || dtuRxLen >= DTU_RX_BUF_SIZE)
    {
      /* 确保字符串结尾 */
      if (dtuRxLen < DTU_RX_BUF_SIZE)
        dtuRxBuf[dtuRxLen] = '\0';
      else
        dtuRxBuf[DTU_RX_BUF_SIZE - 1] = '\0';

      char *pBuf = (char *)dtuRxBuf;

      /* 解析 CSQ 信号质量: config,csq,ok,<value> */
      char *pCsq = strstr(pBuf, "config,csq,ok,");
      if (pCsq)
      {
        int csqVal = 0;
        if (sscanf(pCsq, "config,csq,ok,%d", &csqVal) == 1)
        {
          int percent = csqVal * 100 / 31;
          printf("[DTU] 信号强度: %d (%d%%)\r\n", csqVal, percent);
          dtuCsqRssi = csqVal;  // 保存 CSQ 值供对时上报使用
        }
      }

      /* 解析网络时间: config,nettime,ok,年,月,日,时,分,秒,毫秒 */
      char *pNet = strstr(pBuf, "config,nettime,ok,");
      if (pNet)
      {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
        if (sscanf(pNet, "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d",
                   &y, &mo, &d, &h, &mi, &s, &ms) >= 6)
        {
          printf("[DTU] 网络时间: %04d/%02d/%02d %02d:%02d:%02d\r\n",
                 y, mo, d, h, mi, s);
          /* 保存时间戳供 LoRa 上报使用 */
          snprintf(dtuNetTimeStr, sizeof(dtuNetTimeStr),
                   "%04d%02d%02d%02d%02d%02d", y, mo, d, h, mi, s);
          dtuTimestamp = dateTimeToTimestamp(y, mo, d, h, mi, s);
          dtuSyncTick = HAL_GetTick();  // 记录对时时刻
          printf("[DTU] Unix timestamp: %lu\r\n", (unsigned long)dtuTimestamp);
          dtuTimeValid = 1;  // 标记网络时间已获取
          printf("[DTU] 网络时间获取成功，切换为%lu分钟对时\r\n", (unsigned long)(getConfigIntervalMs() / 60000));

          // 获取到正确时间后，立即上报一次对时信息（入队，由队列控制间隔）
          sendTimeSyncToDtu();
          // 开机后首次获取到网络时间，上报一次配置信息（仅上报一次）
          if (!configBootSent)
          {
            configBootSent = 1;
            sendConfigToDtu();
          }
        }
      }

      /* 都没匹配到，原样打印明文 */
      if (!pCsq && !pNet)
      {
        /* JSON 解析 DTU 下发的指令 */
        parseDtuCommand(pBuf);
      }

      dtuRxLen = 0;
      /* 重新启动中断接收 */
      HAL_UART_Receive_IT(&huart3, dtuRxBuf, 1);
    }

    /* 向 DTU 发送 csq + nettime 查询
     * 未获取到时间前：每 5 秒查询一次
     * 获取到时间后：由 configStr 配置决定（默认 30 分钟，范围 5~60 分钟） */
    {
      uint32_t queryInterval = dtuTimeValid ? getConfigIntervalMs() : 5000;
      if (HAL_GetTick() - dtuCsqTick >= queryInterval)
      {
        dtuCsqTick = HAL_GetTick();
        uint8_t cmd1[] = "config,get,csq\r\n";
        HAL_UART_Transmit(&huart3, cmd1, sizeof(cmd1) - 1, 1000);
        HAL_Delay(300);  // 等待 csq 响应
        uint8_t cmd2[] = "config,get,nettime\r\n";
        HAL_UART_Transmit(&huart3, cmd2, sizeof(cmd2) - 1, 1000);
        if (!dtuTimeValid)
          printf("[DTU] Waiting for net time... (retry %lus)\r\n", queryInterval / 1000);
      }
    }

    HAL_Delay(1);  // 短延时
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */
}



/**
  * @brief  GPIO 初始化函数
  * @note   配置说明：
  *         1. LED 指示灯：
  *            - LED1 (PB3) - 输出，推挽，低速，默认灭
  *            - LED2 (PB4) - 输出，推挽，低速，默认灭
  *            - LED3 (PB5) - 输出，推挽，低速，默认灭
  *            - LED4 (PA0) - 输出，推挽，低速，默认灭（心跳指示灯）
  *         2. 按键输入：
  *            - K1 (PB4) - 外部中断，下降沿触发，无上下拉
  *            - K2 (PB5) - 外部中断，下降沿触发，无上下拉
  *         3. LoRa SPI 引脚（软件模拟 SPI）：
  *            - SCK (PA5) - 输出，推挽，高速
  *            - MOSI (PA7) - 输出，推挽，高速
  *            - MISO (PA6) - 输入，无上下拉
  *         4. LoRa 控制引脚：
  *            - CSN (PB0) - 输出，推挽，高速，默认高电平（未选中）
  *            - RST (PA1) - 输出，推挽，高速，默认高电平（不复位）
  *         5. LoRa 中断引脚：
  *            - IRQ (PA2) - 外部中断，上升沿触发，下拉
  * @param  None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* 使能 GPIO 端口时钟 */
  __HAL_RCC_GPIOC_CLK_ENABLE();  // GPIOC 时钟
  __HAL_RCC_GPIOD_CLK_ENABLE();  // GPIOD 时钟
  __HAL_RCC_GPIOA_CLK_ENABLE();  // GPIOA 时钟
  __HAL_RCC_GPIOB_CLK_ENABLE();  // GPIOB 时钟

  /* 配置 GPIO 输出引脚初始电平 */
  HAL_GPIO_WritePin(GPIOB, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);  // LED1/2/3 默认灭

  /* 配置 LED1/LED2/LED3 引脚 (PB3/PB4/PB5) */
  GPIO_InitStruct.Pin = LED1_Pin|LED2_Pin|LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;      // 低速
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 配置按键 K1/K2 引脚 (PB4/PB5) */
  GPIO_InitStruct.Pin = K1_Pin|K2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;      // 外部中断，下降沿触发
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 配置 EXTI 中断优先级 */
  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);           // K1 中断优先级（最高）
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);                   // 使能 K1 中断

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);         // K2 中断优先级（最高）
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);                 // 使能 K2 中断

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* 配置 LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;             // 复用推挽输出（硬件 SPI）
  GPIO_InitStruct.Pull = GPIO_NOPULL;                 // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;       // 高速
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 配置 LoRa CSN 片选引脚 (PB0) - 默认高电平（未选中） */
  GPIO_InitStruct.Pin  = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;     // 高速
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);  // CSN 拉高

  /* 配置 LoRa RST 复位引脚 (PA1) - 默认高电平（不复位） */
  GPIO_InitStruct.Pin  = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;     // 高速
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);  // RST 拉高

  /* 配置 LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;       // 外部中断，上升沿触发
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;             // 下拉（默认低电平）
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
  * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI）
  * @note   配置 SPI1 外设：
  *         - 主机模式，全双工，8-bit 数据
  *         - CPOL=0, CPHA=0（空闲时 SCK 低电平，第一边沿采样）
  *         - MSB 优先，软件 NSS，波特率分频 16（72MHz/16 = 4.5MHz）
  * @param  None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;     // CPOL=0
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;         // CPHA=0
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;  // 72/16 = 4.5MHz
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  NVIC 中断控制器初始化函数（LoRa IRQ）
  * @note   配置 LoRa 模块的 IRQ 引脚 (PA2) 对应的外部中断 EXTI2
  *         - 中断优先级: 抢占优先级 0，子优先级 0（最高优先级）
  *         - 触发方式: 上升沿触发（在 GPIO 初始化中配置）
  * @retval None
  */
static void MX_NVIC_Init(void)
{
  /* EXTI2 中断用于 LoRa IRQ (PA2) */
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);   // 设置最高优先级
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);           // 使能 EXTI2 中断

  /* USART3 中断用于 DTU 数据接收 */
  HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/* USER CODE BEGIN 4 */

/**
  * @brief  GPIO 外部中断回调函数
  * @note   当 EXTI 中断触发时，HAL 库会调用此函数
  *         根据中断引脚判断中断源，执行相应处理
  * @param  GPIO_Pin: 触发中断的引脚号
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* LoRa IRQ 中断: PA2 -> 调用射频中断处理函数 */
    if (GPIO_Pin == GPIO_PIN_2)
    {
        rf_irq_process();  // 处理 LoRa 模块的中断事件
    }

    /* 按键 K1 中断: PB4 -> 切换到接收模式（带 300ms 消抖） */
    if (GPIO_Pin == K1_Pin)
    {
        // 确认按键按下（低电平有效）
        if (HAL_GPIO_ReadPin(K1_GPIO_Port, K1_Pin) == GPIO_PIN_RESET)
        {
            // 300ms 消抖保护，防止多次触发
            if (HAL_GetTick() - lastKeyTick > 300)
            {
                lastKeyTick = HAL_GetTick();  // 更新消抖时间戳
                enter_rx_mode();  // 切换到接收模式
            }
        }
    }

    /* 按键 K2 中断: PB5 -> 切换到发送模式（带 300ms 消抖） */
    if (GPIO_Pin == K2_Pin)
    {
        // 确认按键按下（低电平有效）
        if (HAL_GPIO_ReadPin(K2_GPIO_Port, K2_Pin) == GPIO_PIN_RESET)
        {
            // 300ms 消抖保护，防止多次触发
            if (HAL_GetTick() - lastKeyTick > 300)
            {
                lastKeyTick = HAL_GetTick();  // 更新消抖时间戳
                workMode = MODE_TX;           // 设置工作模式为发送
                lastTxTick = 0;               // 强制立即发送第一帧
                printf("\r\n>> Switch to TX mode\r\n");
            }
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief USART3 Initialization Function (DTU 通信)
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }

  /* 启动中断接收，每个字节触发一次回调 */
  HAL_UART_Receive_IT(&huart3, dtuRxBuf, 1);
}

/**
  * @brief  USART3 接收完成回调（HAL 库弱函数重写）
  * @note   每收到 1 字节触发一次，记录时间戳并续接缓冲区
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    dtuRxLen++;
    dtuLastRxTick = HAL_GetTick();  // 记录收到字节的时间
    if (dtuRxLen < DTU_RX_BUF_SIZE)
    {
      HAL_UART_Receive_IT(&huart3, &dtuRxBuf[dtuRxLen], 1);
    }
    else
    {
      dtuRxLen = 0;  // 缓冲区满，丢弃
    }
  }
}

/**
  * @brief  错误处理函数
  * @note   当系统发生严重错误时调用此函数
  *         关闭所有中断，进入死循环
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();  // 关闭所有中断
  while (1)
  {
    // 死循环，等待硬件复位
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  断言失败回调函数
  * @note   当 assert_param 检查失败时调用此函数
  *         可以在此处添加调试信息，输出文件名和行号
  * @param  file: 指向源文件名的指针
  * @param  line: 断言失败的源代码行号
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* 可以在此处添加调试输出，例如：
   * printf("Wrong parameters value: file %s on line %d\r\n", file, line);
   */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
