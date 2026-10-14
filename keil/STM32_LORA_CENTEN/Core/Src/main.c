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

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LY-020 LoRa Gps"  // 工程名称
#define DEMO_VER     "V1.0"                  // 演示版本号
#define EXAMPLE_DATE "2026/10/04"            // 工程日期

#define RX_LEN       36                      // LoRa 接收缓冲区最大长度（字节）

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
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;  // USART3 用于 DTU 通信



static char deviceName[] = "v4-108";            // 设备名称

/* USER CODE BEGIN PV */
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

/* Flash 配置存储 */
#define CONFIG_FLASH_ADDR  0x0800FC00  // Flash 最后一页（STM32F103C8T6 64KB）
#define CONFIG_MAGIC       0xA5A5A5A5  // 有效数据标志

/* USER CODE END PV */
/* Private function prototypes -----------------------------------------------*/
// 系统函数声明
void SystemClock_Config(void);       // 系统时钟配置函数
static void MX_GPIO_Init(void);      // GPIO 初始化函数
static void MX_USART1_UART_Init(void); // USART1 初始化（调试串口）
static void MX_USART3_UART_Init(void); // USART3 初始化（DTU 通信）
static void MX_NVIC_Init(void);      // NVIC 中断控制器初始化

/* USER CODE BEGIN PFP */
// 用户自定义函数声明
static void printf_logo(void);       // 打印工程信息 Logo
static void OnSlave(void);           // LoRa 从机接收处理函数
static void enter_rx_mode(void);     // 切换到接收模式
static void do_tx_send(void);        // 发送模式处理函数
static void sendGpsToDtu(const char *loraStr, int rssi, int snr);  // GPS信息上报DTU
static void sendTimeSyncToDtu(void);        // 对时信息上报DTU
static void sendConfigToDtu(void);          // 配置信息上报DTU
static void loadConfigFromFlash(void);      // 从 Flash 加载配置
static void saveConfigToFlash(const char *str);  // 保存配置到 Flash
static void parseDtuCommand(char *buf);      // JSON 解析 DTU 下发指令
static uint32_t getCurrentTimestamp(void);   // 获取当前实时时间戳
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour, int min, int sec);  // 日期转Unix时间戳

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */



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
      // 上报给 DTU
      sendGpsToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);

      // 延时 800ms 后下发对时信息
      HAL_Delay(800);

      // 构造下发数据: 4|deviceName|timestamp|
      char txData[64];
      int txLen = snprintf(txData, sizeof(txData), "%d|%s|%lu|",
                           MSG_TYPE_SYN_TIME, deviceName, (unsigned long)getCurrentTimestamp());

      // 发送 LoRa 数据
      uint32_t tx_time = 0;
      rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
      printf("[LoRa TX] %s\r\n", txData);

      // 等待发送完成
      uint32_t waitStart = HAL_GetTick();
      while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE)
      {
        if (HAL_GetTick() - waitStart > 3000) break;  // 3秒超时保护
      }
      rf_set_transmit_flag(RADIO_FLAG_IDLE);
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
    printf("");
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
  // 每月天数累加（非闰年）
  static const int daysBeforeMonth[] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
  int leapYears;
  uint32_t days, timestamp;

  year -= 1900;
  leapYears = ((year + 69) / 4) - ((year + 69) / 100) + ((year + 369) / 400);
  days = (uint32_t)(year - 70) * 365 + leapYears + daysBeforeMonth[month] + day - 1;
  if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days++;
  timestamp = days * 86400UL + (uint32_t)hour * 3600 + (uint32_t)min * 60 + (uint32_t)sec;
  return timestamp;
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
  * @brief  将LoRa收到的GPS信息通过DTU串口上报（JSON格式）
  * @param  loraStr: LoRa 原始载荷字符串（如 "1|v4-88|26.12345,109.54321|"）
  * @param  rssi:    接收信号强度 (dBm)
  * @param  snr:     信噪比 (dB)
  * @retval None
  */
static void sendGpsToDtu(const char *loraStr, int rssi, int snr)
{
  static char txBuf[256];
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
    HAL_UART_Transmit(&huart3, (uint8_t *)txBuf, len, 1000);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 100);
    printf("[DTU TX] %s\r\n", txBuf);
  }
}

/**
  * @brief  获取到网络时间后，上报一次对时信息给 DTU（JSON格式）
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

  static char txBuf[256];
  int len = snprintf(txBuf, sizeof(txBuf),
      "{"
      "\"version\":\"1.0\","
      "\"method\":\"thing.event.property.post\","
      "\"params\":{"
      "\"lorainfo\":\"%s\","
      "\"upDateDevice\":\"%s\","
      "\"tm\":%lu,"
      "\"rssi\":%d,"
      "\"snr\":0"
      "}"
      "}",
      dataStr, (char *)deviceName, (unsigned long)getCurrentTimestamp(), dtuCsqRssi);

  if (len > 0 && len < (int)sizeof(txBuf))
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)txBuf, len, 1000);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 100);
    printf("[DTU TX] %s\r\n", txBuf);
  }
}

/**
  * @brief  开机网络正常后，上报一次配置信息给 DTU（JSON格式）
  * @note   数据格式: 6|deviceName|config_str|count
  */
static void sendConfigToDtu(void)
{
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|%lu",
           MSG_TYPE_CONFIG, deviceName,
           configStr,
           (unsigned long)rtcSendCount++);

  static char txBuf[256];
  int len = snprintf(txBuf, sizeof(txBuf),
      "{"
      "\"version\":\"1.0\","
      "\"method\":\"thing.event.property.post\","
      "\"params\":{"
      "\"lorainfo\":\"%s\","
      "\"upDateDevice\":\"%s\","
      "\"tm\":%lu,"
      "\"rssi\":%d,"
      "\"snr\":0"
      "}"
      "}",
      dataStr, (char *)deviceName, (unsigned long)getCurrentTimestamp(), dtuCsqRssi);

  if (len > 0 && len < (int)sizeof(txBuf))
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)txBuf, len, 1000);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 100);
    printf("[DTU TX] %s\r\n", txBuf);
  }
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
  }

  printf("[DTU JSON] cmd=%s, deviceId=%s, value=%s\r\n", cmd, devId, val);

  /* Step 3: 检查 deviceId 是否为本机 */
  if (strcmp(devId, (char *)deviceName) != 0)
  {
    printf("[DTU CMD] Not this device (target=%s, local=%s), skip\r\n", devId, deviceName);
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
  MX_GPIO_Init();           // GPIO 初始化（LED、按键、LoRa SPI 引脚）
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
      printf("[TIME] %s\r\n", timeBuf);
    }

    /* PB15 (LED1) 接收指示：点亮500ms后自动熄灭 */
    if (led1On && HAL_GetTick() - led1OnTick >= 500)
    {
      led1On = 0;
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }

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
          printf("[DTU] 网络时间获取成功，切换为10分钟对时\r\n");

          // 获取到正确时间后，立即上报一次对时信息
          sendTimeSyncToDtu();
          HAL_Delay(1000);  // 间隔 1 秒，避免 DTU 无法识别
          // 开机网络正常后，上报一次配置信息
          sendConfigToDtu();
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
     * 获取到时间后：每 10 分钟查询一次 */
    {
      uint32_t queryInterval = dtuTimeValid ? 600000 : 5000;
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

  /* 配置 LoRa SPI 时钟和 MOSI 引脚 (PA5/PA7) - 软件 SPI 输出 */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;     // 高速（SPI 时序要求）
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 配置 LoRa SPI MISO 引脚 (PA6) - 软件 SPI 输入 */
  GPIO_InitStruct.Pin  = LORA_MISO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;           // 输入模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
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
