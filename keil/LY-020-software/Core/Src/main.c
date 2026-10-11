/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 收发器主程序 - STM32F103C8T6
  *                   与 CenterReceiveBLE_v4 LoRa 配置匹配
  * @note           功能说明：
  *                 1. LoRa 通信：支持接收和发送两种模式，可通过按键 K1/K2 切换
  *                 2. GPS 定位：通过 USART3 接收 GPS 模块 NMEA 数据，解析 GGA 语句
  *                 3. LED 指示：LED4 心跳指示，LED1 模式指示，接收/发送状态指示
  *                 4. 调试输出：通过 USART1 输出系统状态和 GPS 数据
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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LY-020 LoRa Gps"  // 工程名称
#define DEMO_VER     "V1.0"                  // 演示版本号
#define EXAMPLE_DATE "2026/10/04"            // 工程日期

#define RX_LEN       64                      // LoRa 接收缓冲区最大长度（字节）

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
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// LED 控制相关时间戳
static uint32_t lastLedTick = 0;    // LED4 心跳指示灯上次翻转时间（1秒间隔）
static uint32_t lastLed1Tick = 0;   // LED1 模式指示灯上次翻转时间（RX模式500ms闪烁）

// 工作模式相关
static uint8_t  workMode = MODE_IDLE;  // 当前工作模式（RX/TX/IDLE）
static uint8_t  txMsg[] = "I LOVE YOU"; // TX模式下循环发送的测试消息
static uint32_t lastTxTick = 0;        // 上次发送时间戳（3秒间隔）
static uint32_t txLedOnTick = 0;       // TX LED 点亮时间戳（500ms后熄灭）
static uint32_t lastKeyTick = 0;       // 按键消抖时间戳（300ms消抖间隔）

/* LoRa receive buffers - LoRa 接收缓冲区 */
static uint8_t  prev_rx_buf[RX_LEN] = {0};  // 上次接收到的数据（用于对比是否重复）
static uint8_t  prev_rx_size = 0;           // 上次接收数据的长度

extern struct RxDoneMsg RxDoneParams;  // 外部声明：接收完成时的消息结构体（在驱动中定义）

/* GPS UART receive buffer - GPS 串口接收缓冲区 */
#define GPS_BUF_SIZE  256                          // GPS 接收缓冲区大小
static uint8_t  gpsRxBuf[GPS_BUF_SIZE];            // GPS NMEA 语句接收缓冲区
static uint16_t gpsRxIdx = 0;                      // 当前接收索引位置
static volatile uint8_t gpsDataReady = 0;          // GPS 数据就绪标志（中断中置1，主循环清零）

/* Parsed GPS data - GPS 解析后的数据结构 */
typedef struct {
  char   time[16];      /* UTC 时间格式: hhmmss.ss */
  char   lat[16];       /* 纬度格式: ddmm.mmmm */
  char   latDir;        /* 纬度方向: N(北) 或 S(南) */
  char   lon[16];       /* 经度格式: dddmm.mmmm */
  char   lonDir;        /* 经度方向: E(东) 或 W(西) */
  uint8_t fixQuality;   /* 定位质量: 0=无效, 1=GPS定位, 2=DGPS差分定位 */
  uint8_t satellites;   /* 可见卫星数量 */
  uint8_t valid;        /* 定位有效性: 1=有效定位, 0=未定位 */
} GpsData_t;
static GpsData_t gpsData = {0};              // GPS 解析数据实例
static uint32_t lastGpsPrintTick = 0;        // GPS 数据打印时间戳（1秒间隔）
static uint8_t  gpsLastRaw[GPS_BUF_SIZE];    // 最后一条原始 NMEA 语句（用于调试显示）
static uint16_t gpsLastRawLen = 0;           // 原始语句长度
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
// 系统函数声明
void SystemClock_Config(void);       // 系统时钟配置函数
static void MX_GPIO_Init(void);      // GPIO 初始化函数
static void MX_USART1_UART_Init(void); // USART1 初始化（调试串口）
static void MX_USART2_UART_Init(void); // USART2 初始化（GPS 串口）
static void MX_NVIC_Init(void);      // NVIC 中断控制器初始化

/* USER CODE BEGIN PFP */
// 用户自定义函数声明
static void printf_logo(void);       // 打印工程信息 Logo
static void OnSlave(void);           // LoRa 从机接收处理函数
static void LedToggle(void);         // LED4 快速闪烁（接收指示）
static void enter_rx_mode(void);     // 切换到接收模式
static void do_tx_send(void);        // 发送模式处理函数
void gps_uart_receive_byte(void);    // GPS 串口单字节接收（中断调用）
void gps_print_data(void);           // GPS 数据打印函数
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  解析 $GPGGA 或 $GNGGA NMEA 语句，提取时间、坐标、卫星数等信息
  * @param  sentence: 指向完整 NMEA 语句的指针（以 $ 开头）
  * @note   GGA 语句格式: $GPGGA,time,lat,N/S,lon,E/W,fix,sats,hdop,alt,M,...
  *         字段索引:      0    1    2   3   4    5   6    7    8    9  10
  */
static void gps_parse_gga(const char *sentence)
{
  /* 解析流程：逐字符扫描，以逗号为分隔符提取各字段 */
  const char *p = sentence;  // 语句指针
  int field = 0;             // 当前字段索引
  char buf[32];              // 临时字段缓冲区
  int bi = 0;                // 缓冲区索引

  memset(buf, 0, sizeof(buf));  // 清空缓冲区

  // 逐字符遍历语句，提取前8个字段（0-7）
  while (*p && field <= 7)
  {
    if (*p == ',')  // 遇到字段分隔符
    {
      buf[bi] = '\0';  // 字符串结束
      switch (field)
      {
        case 1: /* UTC 时间字段 */
          strncpy(gpsData.time, buf, sizeof(gpsData.time) - 1);
          break;
        case 2: /* 纬度字段 */
          strncpy(gpsData.lat, buf, sizeof(gpsData.lat) - 1);
          break;
        case 3: /* 纬度方向 N/S */
          gpsData.latDir = buf[0];
          break;
        case 4: /* 经度字段 */
          strncpy(gpsData.lon, buf, sizeof(gpsData.lon) - 1);
          break;
        case 5: /* 经度方向 E/W */
          gpsData.lonDir = buf[0];
          break;
        case 6: /* 定位质量标识 */
          gpsData.fixQuality = (uint8_t)atoi(buf);  // 字符串转整数
          gpsData.valid = (gpsData.fixQuality > 0) ? 1 : 0;  // 非0表示定位有效
          break;
        case 7: /* 可见卫星数量 */
          gpsData.satellites = (uint8_t)atoi(buf);
          break;
        default:
          break;
      }
      bi = 0;      // 重置缓冲区索引
      field++;     // 进入下一字段
    }
    else  // 普通字符，存入缓冲区
    {
      if (bi < (int)sizeof(buf) - 1)  // 防止缓冲区溢出
        buf[bi++] = *p;
    }
    p++;  // 移动到下一字符
  }
}

/**
  * @brief  GPS 串口接收处理函数（在 USART3 中断中被调用）
  * @note   功能说明：
  *         1. 从 USART2(实际是USART3) 数据寄存器读取接收到的字节
  *         2. 检测到 '$' 字符时重置接收索引（新语句开始）
  *         3. 将字符存入缓冲区，直到检测到换行符 '\n'
  *         4. 语句接收完成后判断是否为 GGA 语句，若是则进行解析
  *         5. 设置数据就绪标志，通知主循环打印数据
  */
void gps_uart_receive_byte(void)
{
  uint8_t ch;  // 临时存储接收的字符
  
  // 检查 USART 状态寄存器的接收数据寄存器非空标志位
  if (huart2.Instance->SR & USART_SR_RXNE)
  {
    // 从数据寄存器读取一个字节（低8位有效）
    ch = (uint8_t)(huart2.Instance->DR & 0xFF);
    
    // 检测到语句起始符 '$'
    if (ch == '$')
    {
      gpsRxIdx = 0;  // 重置缓冲区索引，准备接收新语句
    }
    
    // 将字符存入缓冲区（防止溢出）
    if (gpsRxIdx < GPS_BUF_SIZE - 1)
    {
      gpsRxBuf[gpsRxIdx++] = ch;
    }
    
    // 检测到语句结束符 '\n'（完整语句接收完成）
    if (ch == '\n')
    {
      gpsRxBuf[gpsRxIdx] = '\0';  // 添加字符串结束符
      
      /* 保存原始语句用于调试显示 */
      memcpy(gpsLastRaw, gpsRxBuf, gpsRxIdx + 1);
      gpsLastRawLen = gpsRxIdx;
      
      /* 判断是否为 GGA 语句（支持 GPS 和北斗双模） */
      if (strncmp((char *)gpsRxBuf, "$GPGGA", 6) == 0 ||   // GPS GGA 语句
          strncmp((char *)gpsRxBuf, "$GNGGA", 6) == 0)     // 北斗 GGA 语句
      {
        gps_parse_gga((char *)gpsRxBuf);  // 解析 GGA 语句
      }
      
      gpsDataReady = 1;  // 设置数据就绪标志（主循环中会清零）
      gpsRxIdx = 0;      // 重置索引，准备接收下一条语句
    }
  }
}

/**
  * @brief  GPS 数据打印函数（在主循环中调用）
  * @note   每秒检查一次数据就绪标志，若有效则打印解析后的 GPS 数据
  *         包括：时间、纬度、经度、卫星数、定位状态、原始语句
  */
void gps_print_data(void)
{
  uint32_t now = HAL_GetTick();  // 获取当前系统时间（毫秒）
  
  // 1秒打印间隔控制
  if (now - lastGpsPrintTick >= 1000)
  {
    lastGpsPrintTick = now;
    
    // 检查是否有新数据
    if (gpsDataReady)
    {
      if (gpsData.valid)  // 定位有效
      {
        printf("[GPS] Time:%s Lat:%s%c Lon:%s%c Sats:%d FIX | RAW(%d): %s",
               gpsData.time,                                              // UTC 时间
               gpsData.lat, gpsData.latDir,                               // 纬度+方向
               gpsData.lon, gpsData.lonDir,                               // 经度+方向
               gpsData.satellites,                                        // 卫星数
               gpsLastRawLen, (char *)gpsLastRaw);                        // 原始语句
      }
      else  // 未定位或定位无效
      {
        printf("[GPS] Time:%s Sats:%d NO FIX | RAW(%d): %s",
               gpsData.time,                                              // UTC 时间
               gpsData.satellites,                                        // 卫星数
               gpsLastRawLen, (char *)gpsLastRaw);                        // 原始语句
      }
      gpsDataReady = 0;  // 清除数据就绪标志
    }
  }
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
  * @brief  LED4 快速闪烁函数（接收指示）
  * @note   LED4 亮 50ms 后灭 50ms，用于指示 LoRa 数据接收成功
  */
static void LedToggle(void)
{
  HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);  // LED4 亮
  HAL_Delay(50);                                  // 延时 50ms
  HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);  // LED4 灭
  HAL_Delay(50);                                  // 延时 50ms
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

    LedToggle();  // LED4 快速闪烁，指示接收成功
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
    printf("rx time out\r\n");
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
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);  // 点亮 LED1
        txLedOnTick = now;  // 记录 LED 点亮时间
        printf("TX: I LOVE YOU\r\n");
    }

    /* LED1 点亮 500ms 后自动熄灭 */
    if (HAL_GPIO_ReadPin(LED1_GPIO_Port, LED1_Pin) == GPIO_PIN_SET)
    {
        if (now - txLedOnTick >= 500)
        {
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);  // 熄灭 LED1
        }
    }

    /* 检查并清除发送完成标志 */
    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        rf_set_transmit_flag(RADIO_FLAG_IDLE);  // 清除标志，准备下次发送
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
  *         6. 主循环：LED 心跳、模式处理、GPS 数据打印
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
  MX_USART2_UART_Init();    // USART2 初始化（GPS 串口，9600 波特率）

  /* 初始化 NVIC 中断控制器（LoRa IRQ 使用 PA2 EXTI） */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */

  // 打印启动信息
  printf("\r\n===== LoRa Receiver Start =====\r\n");
  printf_logo();  // 显示工程信息 Logo

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
      /* LED4 心跳指示灯 - 始终闪烁，表示系统仍在运行 */
      if (HAL_GetTick() - lastLedTick >= 1000)
      {
        lastLedTick = HAL_GetTick();
        HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
      }
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);  // LED1 快速闪烁表示错误
      HAL_Delay(100);\n    }
  }
  printf("RF init ok\r\n");

  /* 设置 LoRa 默认参数（与 CenterReceiveBLE_v4 ETSI_868 配置一致）：
   *   频率 = 915 MHz, 扩频因子 SF = 10, 带宽 BW = 125 kHz
   *   编码率 CR = 4/5, 发射功率 = 22 dBm
   * 此函数还会打印: FREQ= xxx  SF=x   BW=x  CR=x */
  rf_set_default_para();

  // 打印 LoRa 配置信息
  printf("===== LoRa Ready =====\r\n");
  printf("  Freq    : 915.000 MHz\r\n");   // 载波频率
  printf("  SF      : 10\r\n");            // 扩频因子
  printf("  BW      : 125 kHz\r\n");       // 信号带宽
  printf("  CR      : 4/5\r\n");           // 编码率
  printf("  Power   : 22 dBm\r\n");        // 发射功率
  printf("  CRC     : OFF\r\n");           // CRC 校验关闭
  printf("  SyncWord: 0x12\r\n");          // 同步字
  printf("----------------------------\r\n");
  printf("  K1 -> RX mode (LED 500ms blink)\r\n");  // 按键 K1 功能说明
  printf("  K2 -> TX mode (send 'I LOVE YOU' / 3s)\r\n");  // 按键 K2 功能说明
  printf("----------------------------\r\n");

  enter_rx_mode();  // 默认进入接收模式

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* LED4 (PA0) 1秒闪烁 - 心跳指示灯（始终运行） */
    if (HAL_GetTick() - lastLedTick >= 1000)
    {
      lastLedTick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    }

    /* 根据当前工作模式执行相应处理 */
    switch (workMode)
    {
      case MODE_RX:  // 接收模式
        /* LED1: 500ms 闪烁，表示处于接收状态 */
        if (HAL_GetTick() - lastLed1Tick >= 500)
        {
          lastLed1Tick = HAL_GetTick();
          HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        }
        OnSlave();  // 调用接收处理函数
        break;

      case MODE_TX:  // 发送模式
        do_tx_send();  // 调用发送处理函数
        break;

      default:  // 空闲模式，不做任何处理
        break;
    }

    /* GPS 数据打印，每秒一次 */
    gps_print_data();

    HAL_Delay(10);  // 延时 10ms，降低 CPU 占用率
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
  * @brief USART2 Initialization Function (GPS, RX only, 9600 baud)
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART3;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  /* Enable RXNE interrupt */
  __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
  /* USER CODE END USART2_Init 2 */
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
  HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);           // LED4 默认灭

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

  /* 配置 LED4 引脚 (PA0) - 心跳指示灯 */
  GPIO_InitStruct.Pin  = LED4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;       // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;               // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;      // 低速
  HAL_GPIO_Init(LED4_GPIO_Port, &GPIO_InitStruct);

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
                HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);  // 关闭 LED1
                printf("\r\n>> Switch to TX mode\r\n");
            }
        }
    }
}

/* USER CODE END 4 */

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
