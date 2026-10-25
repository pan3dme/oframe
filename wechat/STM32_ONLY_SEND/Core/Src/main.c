/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 定时上报节点 - STM32F103C8T6
  *                   移植自 PAN3029_F030 上报逻辑
  * @note           功能说明：
  *                 1. TX 模式：每隔一段时间上报一帧 MSG_TYPE_TIME|设备名|当天秒数|电量|计数|
                       （固定发时间帧；GPS 坐标由 upgps 指令单独触发上报）
  *                 2. 发送后开启 3 秒 RX 窗口，解析对时消息（MSG_TYPE_SYN_TIME）
  *                 3. LED1 指示：发送/接收时闪烁
  *                 4. 调试输出：通过 USART1 输出系统状态
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "pan3029_rf.h"
#include "pan3029_port.h"
#include "radio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LoRa Only Send"
#define DEMO_VER     "V1.0"
#define EXAMPLE_DATE "2026/10/18"

#define TX_LEN       64
#define RX_LEN       64

/* 消息类型定义（与 PAN3029_F030 对齐） */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_WARN     8   /* 告警消息 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* 设备信息 */
#define DEFAULT_DEVICE_NAME  "v0-0"
char deviceName[32] = DEFAULT_DEVICE_NAME;  /* 可变设备名，从 Flash 加载 */
#define BATTERY_VAL  99

/* 配置字符串（持久化到 Flash，可通过指令修改） */
#define DEFAULT_CONFIG_STR  "1,0M,3t,6"
char configStr[64] = DEFAULT_CONFIG_STR;

/* 农场中心坐标（"lat,lng"，空=未设置，由 C 指令下发并持久化） */
char centerStr[64] = "";

/* Flash 存储地址（STM32F103C8T6 64KB，每项独占一页，擦除互不干扰） */
#define DEVNAME_FLASH_ADDR  0x0800F800  /* 设备名存储（倒数第二页） */
#define DEVNAME_MAGIC       0xA5A55A5B  /* 修改魔术字使旧 Flash 数据失效 */
#define CONFIG_FLASH_ADDR   0x0800F400  /* 配置字符串存储（倒数第三页） */
#define CONFIG_MAGIC        0x5A5AA5A6  /* 修改魔术字使旧 Flash 数据失效 */
#define TXPWR_FLASH_ADDR    0x0800F000  /* 发射功率存储（倒数第四页） */
#define TXPWR_MAGIC         0xA5A55A5C  /* 功率魔术字 */
#define CENTER_FLASH_ADDR   0x0800EC00  /* 农场中心坐标存储（倒数第五页） */
#define CENTER_MAGIC        0xA5A55A5D  /* 中心坐标魔术字 */

/* 上报间隔：由 configStr 第一个字段决定（分钟），如 "30,0M,3t,6" = 30 分钟 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;  /* GPS 模块 USART3 (PB11 RX, 9600, 仅接收) */
I2C_HandleTypeDef hi2c1;  /* OLED 模块需要 */

/* USER CODE BEGIN PV */

/* 时间同步变量 */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间戳 */
static uint32_t syncedTick = 0;       /* 同步时的 HAL_GetTick() 值 */
static uint8_t  timeSynced = 0;       /* 是否已同步时间 */
static uint32_t anchorTick = 0;       /* 首次对时的 tick 基准（用于晶振偏差计算） */
static uint32_t anchorTimestamp = 0;  /* 首次对时的 Unix 时间戳基准 */
static uint8_t  configNeedReport = 1;  /* 配置需要上报（开机=1，收到指令更新=1，上报后=0） */
static uint8_t  centerNeedReport = 0;  /* 中心坐标需要上报（收到 C 指令=1，上报后=0） */
static uint8_t  gpsWaitReport = 0;     /* 收到 upgps 指令=1，处理完自动清零 */
static uint8_t  gpsNeedRetry = 0;      /* 上一轮搜星失败(已上报 0.0,0.0)=1
                                        * → 本轮跳过定时上报，直接重试搜星上报坐标；
                                        *   成功拿到坐标后自动清零，恢复定时上报
                                        * @note  RAM 在 deepsleep 期间保持，故跨休眠有效；
                                        *        但真正掉电重启会丢失(回到0)，这是可接受的 */
#define TX_COUNT_MAX   999             /* 上报帧计数上限：超过后回绕到 1
                                        * 好处是帧里这一字段恒为 1~3 位数字，
                                        * 帧长稳定，也不用担心 uint32 溢出(约 49 天后) */

static uint32_t tx_count = 0;          /* 上报帧计数（所有帧类型共用，每周期自增一次）
                                        * 放在文件作用域：配置帧/中心坐标帧/定位帧
                                        * 也都要带这个计数，便于云端判重和算丢包 */
static uint32_t nextTxTimestamp = 0;   /* 下次上报的绝对时间戳（秒） */
static uint8_t  currentTxPower = DEFAULT_PWR;  /* 当前发射功率（从 Flash 加载或指令修改） */
static uint8_t  loraInitialized = 0;  /* LoRa 是否已初始化（首次 TX 前才初始化） */

/* --- GPS 模块（USART3, PB11 RX, 9600）---
 * @note  本版本不做 GPS 电源三级管/复位控制，模块常供电，直接接收 NMEA
 *        设计要点：中断里只做"塞入环形缓冲"这个最轻的动作，
 *        解析和打印全部放在主循环，避免在 ISR 里跑串处理和 printf
 */
#define GPS_RING_LEN   512   /* 环形缓冲：主循环被 LoRa 收发阻塞时先攒着 */
#define GPS_LINE_LEN   128   /* 单条 NMEA 语句上限 */
#define GPS_FIX_TTL_MS 60000 /* 定位有效期(ms)：超过这么久没刷新就算失锁 */
#define GPS_WAIT_TIMEOUT_MS 180000 /* upgps 搜星上限 = 3 分钟；超时则上报 0.0,0.0 */
#define GPS_WAIT_MIN_MS     10000  /* upgps 最短搜星时间 = 10 秒
                                    * @note 收到指令后不能马上发 LoRa：中继此时还没准备好接收，
                                    *       发太快大概率丢帧。即使立刻就有坐标，也要至少等够这 10 秒 */

typedef struct {
  uint8_t  valid;  /* 已定位 */
  double   lat;    /* 纬度（北纬为正） */
  double   lng;    /* 经度（东经为正） */
  float    alt;    /* 海拔 (m) */
  float    speed;  /* 地面速度 (km/h) */
  uint8_t  sats;   /* 参与定位的卫星数 */
} GpsFix_t;

static GpsFix_t     gpsFix;
static uint8_t      gpsFixValid = 0;   /* 最近一帧 GGA 是否有效定位 */
static uint32_t     gpsRxCount = 0;    /* 收到的字节数（诊断用） */
static uint32_t     gpsLastHint = 0;   /* 未定位提示的节流时间戳 */
static uint32_t     gpsFixTick = 0;    /* 最近一次拿到有效定位的时刻（HAL_GetTick） */
static uint8_t      gpsPrintEn = 0;    /* GPS 打印总开关（1=打印, 0=静默）
                                        * 默认 0：正常启动/运行不打任何 GPS 日志；
                                        * 收到 upgps 后临时置 1，处理完自动恢复原值；
                                        * 也可用 sendmode 指令手动开（调试用）
                                        * @note  只影响打印，不影响接收/解析/上报 */
static uint8_t      gpsPrintSaved = 0; /* upgps 处理前的打印状态，用于事后恢复 */

static volatile uint8_t  gpsRing[GPS_RING_LEN];
static volatile uint16_t gpsWr = 0;    /* 写指针（ISR 更新） */
static volatile uint16_t gpsRd = 0;    /* 读指针（主循环更新） */
static volatile uint8_t  gpsByte = 0;  /* HAL_UART_Receive_IT 的单字节缓冲 */
static char     gpsLine[GPS_LINE_LEN];
static uint16_t gpsLineLen = 0;

extern struct RxDoneMsg RxDoneParams;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_NVIC_Init(void);

/* USER CODE BEGIN PFP */
static void printf_logo(void);
static uint32_t getTodaySecond(void);
static uint32_t getCurrentTimestamp(void);
static uint8_t isLeapYear(uint16_t year);
static void formatTimestamp(uint32_t ts, char *buf, uint8_t len);
static void OnMaster(void);
static void parse_rx_message(uint8_t *buf, uint8_t len);
static void LedToggle(void);
static void loadDeviceNameFromFlash(void);
static void saveDeviceNameToFlash(const char *name);
static void loadConfigFromFlash(void);
static void saveConfigToFlash(const char *str);
static void loadTxPowerFromFlash(void);
static void saveTxPowerToFlash(uint8_t power);
static void loadCenterFromFlash(void);
static void saveCenterToFlash(const char *str);
static uint8_t centerCoordValid(double lat, double lng);
static void loraInitFirstTime(void);
static void sendConfigMessage(void);
static void sendCenterMessage(void);
static void sendGpsPositionMessage(uint8_t hasFix);
static uint8_t gpsSearchAndReport(void);
static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs);
static uint32_t getConfigIntervalSec(void);
static uint8_t  getDeviceIndex(void);
static uint32_t getSlotOffsetSec(void);
static void calcNextTxTime(void);
static void MX_USART3_UART_Init(void);
static void gpsTask(void);
static void parseNmeaLine(char *line);
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen);
static double nmeaToDeg(const char *v, char dir);
static uint8_t gpsFixFresh(void);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
  * @brief  printf 底层输出函数重定向（USART1）
  */
PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

/**
  * @brief  打印工程信息 Logo
  */
static void printf_logo(void)
{
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* Project Name  : %s\r\n", EXAMPLE_NAME);
  printf("* Demo Version  : %s\r\n", DEMO_VER);
  printf("* Date          : %s\r\n", EXAMPLE_DATE);
  printf("* MCU           : STM32F103C8T6\r\n");
  printf("* Radio         : PAN3029 (LoRa)\r\n");
  printf("*************************************************************\r\n");
}

/**
  * @brief  从 Flash 加载设备名
  * @note   如果 Flash 中有有效魔术字则加载，否则使用默认名
  */
static void loadDeviceNameFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)DEVNAME_FLASH_ADDR;
  if (magic == DEVNAME_MAGIC) {
    const char *p = (const char *)(DEVNAME_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(deviceName) - 1 && p[i] != '\0'; i++)
      deviceName[i] = p[i];
    deviceName[i] = '\0';
    printf("[Flash] 设备名已加载: %s\r\n", deviceName);
  } else {
    printf("[Flash] 无保存设备名, 使用默认: %s\r\n", deviceName);
  }
}

/**
  * @brief  保存设备名到 Flash
  * @param  name: 要保存的设备名字符串
  */
static void saveDeviceNameToFlash(const char *name)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;
  uint8_t i;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = DEVNAME_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  /* 写入魔术字 */
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DEVNAME_FLASH_ADDR, DEVNAME_MAGIC);

  /* 写入设备名字符串（按半字写入） */
  for (i = 0; i < strlen(name) + 1; i += 2) {
    uint16_t halfWord = name[i] | (name[i + 1] << 8);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, DEVNAME_FLASH_ADDR + 4 + i,
                      halfWord);
  }

  HAL_FLASH_Lock();
  printf("[Flash] 设备名已保存: %s\r\n", name);
}

/**
  * @brief  从 Flash 加载配置字符串
  */
static void loadConfigFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)CONFIG_FLASH_ADDR;
  if (magic == CONFIG_MAGIC) {
    const char *p = (const char *)(CONFIG_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(configStr) - 1 && p[i] != '\0'; i++)
      configStr[i] = p[i];
    configStr[i] = '\0';
    printf("[Flash] 配置已加载: %s\r\n", configStr);
  } else {
    printf("[Flash] 无保存配置, 使用默认: %s\r\n", configStr);
  }
}

/**
  * @brief  保存配置字符串到 Flash
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

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_FLASH_ADDR, CONFIG_MAGIC);

  for (i = 0; i < strlen(str) + 1; i += 2) {
    uint16_t halfWord = str[i] | (str[i + 1] << 8);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, CONFIG_FLASH_ADDR + 4 + i,
                      halfWord);
  }

  HAL_FLASH_Lock();
  printf("[Flash] 配置已保存: %s\r\n", str);
}

/**
  * @brief  从 Flash 加载发射功率
  */
static void loadTxPowerFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)TXPWR_FLASH_ADDR;
  if (magic == TXPWR_MAGIC) {
    uint8_t saved = (uint8_t)(*(volatile uint32_t *)(TXPWR_FLASH_ADDR + 4));
    if (saved >= 4 && saved <= 20) {
      currentTxPower = saved;
      printf("[Flash] 发射功率已加载: %d dBm\r\n", currentTxPower);
    } else {
      printf("[Flash] 发射功率超范围(%d), 使用默认: %d\r\n",
             saved, DEFAULT_PWR);
    }
  } else {
    printf("[Flash] 无保存发射功率, 使用默认: %d dBm\r\n", DEFAULT_PWR);
  }
}

/**
  * @brief  保存发射功率到 Flash
  */
static void saveTxPowerToFlash(uint8_t power)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = TXPWR_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, TXPWR_FLASH_ADDR, TXPWR_MAGIC);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, TXPWR_FLASH_ADDR + 4, (uint32_t)power);

  HAL_FLASH_Lock();
  printf("[Flash] 发射功率已保存: %d dBm\r\n", power);
}

/**
 * @brief  从 Flash 读出一个以 '\0' 结尾的字符串
 */
static void flashReadString(uint32_t addr, char *dst, int dstSize)
{
  const char *p = (const char *)addr;
  int i;
  for (i = 0; i < dstSize - 1 && p[i] != '\0'; i++)
    dst[i] = p[i];
  dst[i] = '\0';
}

/**
 * @brief  把一个字符串（含结尾 '\0'）按半字写入 Flash
 * @note   STM32F1 只能半字编程；用 i+1<=len 兜底，
 *         避免读到源串 '\0' 之后的越界字节
 */
static void flashWriteString(uint32_t addr, const char *str)
{
  int len = (int)strlen(str);
  int i;
  for (i = 0; i <= len; i += 2) {
    uint16_t halfWord = (uint16_t)str[i];
    if (i + 1 <= len)
      halfWord |= ((uint16_t)str[i + 1]) << 8;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, halfWord);
  }
}

/**
 * @brief  从 Flash 加载农场中心坐标
 * @note   如果 Flash 中有有效魔术字则加载，否则保持为空（未设置）
 */
static void loadCenterFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)CENTER_FLASH_ADDR;
  if (magic == CENTER_MAGIC) {
    flashReadString(CENTER_FLASH_ADDR + 4, centerStr, sizeof(centerStr));
    printf("[Flash] 中心坐标已加载: %s\r\n", centerStr);
  } else {
    centerStr[0] = '\0';
    printf("[Flash] 无保存中心坐标\r\n");
  }
}

/**
 * @brief  保存农场中心坐标到 Flash（独立页，与其它配置项互不影响）
 * @param  str: 归一化后的坐标字符串，格式 "lat,lng"
 */
static void saveCenterToFlash(const char *str)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = CENTER_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CENTER_FLASH_ADDR, CENTER_MAGIC);
  flashWriteString(CENTER_FLASH_ADDR + 4, str);

  HAL_FLASH_Lock();
  printf("[Flash] 中心坐标已保存: %s\r\n", str);
}

/**
 * @brief  校验经纬度是否在合法地理范围内
 * @retval 1=合法, 0=非法
 */
static uint8_t centerCoordValid(double lat, double lng)
{
  if (lat < -90.0 || lat > 90.0)
    return 0;
  if (lng < -180.0 || lng > 180.0)
    return 0;
  return 1;
}

/**
  * @brief  首次初始化 LoRa 模块（从关机状态启动）
  * @note   仅在第一次 TX 前或从 deepsleep 唤醒后调用
  */
static void loraInitFirstTime(void)
{
  printf("LoRa 初始化中...\r\n");

  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  if (rf_init() != RF_OK) {
    printf("射频初始化失败! 请检查接线\r\n");
    while (1) {
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
      HAL_Delay(100);
    }
  }
  printf("射频初始化成功\r\n");

  rf_set_default_para();
  rf_set_tx_power(currentTxPower);

  printf("===== LoRa 就绪 =====\r\n");
  printf("  Freq    : %d MHz\r\n", DEFAULT_FREQ / 1000000);
  printf("  SF      : %d\r\n", DEFAULT_SF);
  printf("  BW      : 125 kHz\r\n");
  printf("  CR      : 4/5\r\n");
  printf("  功率    : %d dBm (当前)\r\n", currentTxPower);
  printf("  CRC     : OFF\r\n");
  printf("  SyncWord: 0x12\r\n");
  printf("----------------------------\r\n");

  loraInitialized = 1;
}

/**
  * @brief  发送配置消息（重启后只发一次）
  * @note   格式: MSG_TYPE_CONFIG|deviceName|config_str|计数|
  */
  static void sendConfigMessage(void)
  {
   char tx_msg[TX_LEN];
   uint32_t tx_time = 1000;

   snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
            MSG_TYPE_CONFIG, deviceName, configStr, (unsigned long)tx_count);

  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != RF_OK) {
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  }

  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  LedToggle();
  printf("[配置] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  发送农场中心坐标消息（1|deviceName|lat,lng|计数|）
 * @note   本项目无 DTU，回复上报走 LoRa 回到集中器，由集中器转发云端
 *         未设置过坐标时第 3 段发 "0,0"，保证帧结构固定，
 *         云端/小程序 split('|') 不会拿到空串
 */
static void sendCenterMessage(void)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
           MSG_TYPE_GPS, deviceName, coord, (unsigned long)tx_count);

  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != RF_OK) {
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  }

  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  LedToggle();
  printf("[中心坐标] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  发送当前 GPS 实时定位坐标帧（5|deviceName|lat,lng|）
 * @param  hasFix: 1=有有效定位（发实测坐标）, 0=搜星失败（发 "0.0,0.0"）
 * @note   用 MSG_TYPE_UP_GPS(5) 而不是 MSG_TYPE_GPS(1)：
 *         1 已被 C 指令的"农场中心坐标回显"占用，两者含义完全不同：
 *           5 = 本节点自身的实时位置（动态，牛走到哪变到哪）
 *           1 = 农场中心点（静态配置，存在 Flash 里）
 *         搜星失败时也必须发帧，第 3 段填 "0.0,0.0"，
 *         保证云端 split('|') 拿到的段数固定，可直接判定为"无坐标"
 */
static void sendGpsPositionMessage(uint8_t hasFix)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;

  if (hasFix) {
    /* 小数后 5 位：精度约 1.1m，够用且帧长更短 */
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%.5f,%.5f|%lu|",
             MSG_TYPE_UP_GPS, deviceName, gpsFix.lat, gpsFix.lng,
             (unsigned long)tx_count);
  } else {
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
             MSG_TYPE_UP_GPS, deviceName, "0.0,0.0", (unsigned long)tx_count);
  }

  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != RF_OK) {
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  }

  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  LedToggle();
  printf("[定位上报] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  搜星 + 上报坐标（upgps 指令 与 失败自动重试 共用同一套流程）
 * @note   搜星期间临时打开 GPS 打印，结束后恢复成进入前的状态
 * @retval 1=拿到有效坐标并已上报, 0=搜星失败已上报 "0.0,0.0"
 */
static uint8_t gpsSearchAndReport(void)
{
  uint8_t hasFix;

  /* 搜星：最短 GPS_WAIT_MIN_MS(给中继留准备时间), 上限 GPS_WAIT_TIMEOUT_MS */
  hasFix = gpsWaitForFix(GPS_WAIT_MIN_MS, GPS_WAIT_TIMEOUT_MS);
  HAL_Delay(1000);

  /* 无论是否定位成功都上报：失败时坐标段填 "0.0,0.0" */
  sendGpsPositionMessage(hasFix);

  /* 打印状态恢复成进来之前的样子（默认回到静默） */
  gpsPrintEn = gpsPrintSaved;
  printf("[GPS] 上报完毕%s, 恢复GPS打印=%s\r\n",
         hasFix ? "(已定位)" : "(未定位)", gpsPrintEn ? "ON" : "OFF");

  return hasFix;
}

/**
  * @brief  计算下次上报时间
  * @note   已对时：基于时隙调度，nextTx = 下一个 slot 边界（休眠时保持在 RAM）
  *         未对时：nextTx = 当前 boot seconds + 间隔（重启后重新计算）
  */
static void calcNextTxTime(void)
{
  uint32_t intervalSec = getConfigIntervalSec();

  if (timeSynced) {
    /* 已对时：基于当天 00:00 的时隙调度 */
    uint32_t slotOffset = getSlotOffsetSec();
    uint32_t nowSec = getTodaySecond();
    uint32_t currentSlot = nowSec / intervalSec;
    uint32_t nextSlotTime = (currentSlot + 1) * intervalSec + slotOffset;
    /* 如果计算的时间已过，再加一个周期 */
    if (nextSlotTime <= nowSec) {
      nextSlotTime += intervalSec;
    }
    /* 防止对时后等待时间过短导致重复上报（< 半周期则再加一个周期） */
    uint32_t waitSec = nextSlotTime - nowSec;
    if (waitSec < intervalSec / 2) {
      nextSlotTime += intervalSec;
    }
    nextTxTimestamp = getCurrentTimestamp() + (nextSlotTime - nowSec);
    {
      uint32_t nowHms = nowSec % 86400;
      uint32_t nextHms = nextTxTimestamp % 86400;
      printf("[SCHED] synced | now=%lu %02lu:%02lu:%02lu slot=%lu wait=%lu (%lu:%02lu) | nextTx=%lu %02lu:%02lu:%02lu\r\n",
             (unsigned long)nowSec,
             (unsigned long)(nowHms / 3600), (unsigned long)((nowHms % 3600) / 60), (unsigned long)(nowHms % 60),
             (unsigned long)slotOffset,
             (unsigned long)(nextSlotTime - nowSec),
             (unsigned long)((nextSlotTime - nowSec) / 60),
             (unsigned long)((nextSlotTime - nowSec) % 60),
             (unsigned long)nextTxTimestamp,
             (unsigned long)(nextHms / 3600), (unsigned long)((nextHms % 3600) / 60), (unsigned long)(nextHms % 60));
    }
  } else {
    /* 未对时：当前 boot seconds + 间隔 */
    nextTxTimestamp = getCurrentTimestamp() + intervalSec;
    printf("[SCHED] no-sync | interval=%lu (%lu:%02lu) | nextTx=%lu\r\n",
           (unsigned long)intervalSec,
           (unsigned long)(intervalSec / 60), (unsigned long)(intervalSec % 60),
           (unsigned long)nextTxTimestamp);
  }
}

/**
  * @brief  从 configStr 解析上报间隔（秒）
  * @note   configStr 格式: "30,0M,3t,6" → 第一个字段 30 = 30 分钟 = 1800 秒
  *         取值范围: 1~60 分钟
  */
static uint32_t getConfigIntervalSec(void)
{
  uint32_t min = (uint32_t)atoi(configStr);
  if (min == 0) min = 1;    /* 最小 1 分钟 */
  if (min > 60) min = 60;   /* 最大 60 分钟 */
  return min * 60;
}

/**
  * @brief  从 deviceName 解析设备序号
  * @note   "v0-0" → 0, "v0-1" → 1, "v0-27" → 27
  *         格式: "vX-Y" 取 '-' 后面的数字
  */
static uint8_t getDeviceIndex(void)
{
  char *dash = strchr(deviceName, '-');
  if (dash != NULL) {
    return (uint8_t)atoi(dash + 1);
  }
  return 0;
}

/**
  * @brief  计算本设备在上报周期中的时隙偏移（秒）
  * @note   公式: deviceIndex / 30 * intervalSec
  *         如: v0-27, 30 分钟周期 → 27/30*1800 = 1740 秒
  */
static uint32_t getSlotOffsetSec(void)
{
  uint32_t intervalSec = getConfigIntervalSec();
  uint8_t  index = getDeviceIndex();
  return (uint32_t)index * intervalSec / 30;
}

/**
  * @brief  获取当前"当天秒数"
  * @note   未同步时：把开机当作 00:00，返回开机秒数
  *         已同步后：返回真实时间戳 % 86400
  */
static uint32_t getTodaySecond(void)
{
  if (!timeSynced) {
    return HAL_GetTick() / 1000;
  }
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  uint32_t currentTs = syncedTimestamp + elapsed_sec;
  return currentTs % 86400;
}

/**
  * @brief  获取当前完整时间戳
  */
static uint32_t getCurrentTimestamp(void)
{
  if (!timeSynced) {
    return HAL_GetTick() / 1000;
  }
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  return syncedTimestamp + elapsed_sec;
}

/**
  * @brief  判断闰年
  */
static uint8_t isLeapYear(uint16_t year)
{
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/**
  * @brief  将 Unix 时间戳格式化为 "YYYY-MM-DD HH:MM:SS"
  */
static void formatTimestamp(uint32_t ts, char *buf, uint8_t len)
{
  uint32_t sec = ts % 60; ts /= 60;
  uint32_t min = ts % 60; ts /= 60;
  uint32_t hour = ts % 24; ts /= 24;

  uint16_t year = 1970;
  uint8_t month = 1;
  uint8_t day = 1;

  while (1) {
    uint16_t days_in_year = isLeapYear(year) ? 366 : 365;
    if (ts < days_in_year) break;
    ts -= days_in_year;
    year++;
  }

  uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(year)) days_in_month[1] = 29;

  while (month <= 12) {
    if (ts < days_in_month[month - 1]) break;
    ts -= days_in_month[month - 1];
    month++;
  }
  day = ts + 1;

  snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
           year, month, day, hour, min, sec);
}

/**
  * @brief  LED 闪烁一次（亮 50ms → 灭 50ms）
  */
static void LedToggle(void)
{
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  HAL_Delay(50);
}

/**
 * @brief  NMEA 坐标 ddmm.mmmm / dddmm.mmmm → 十进制度
 * @param  v:   原始字段，如 "2629.5800"
 * @param  dir: 'N'/'S'/'E'/'W'，南纬和西经取负
 * @retval 十进制度
 */
static double nmeaToDeg(const char *v, char dir)
{
  double val = atof(v);
  int    deg = (int)(val / 100.0);
  double min = val - (double)deg * 100.0;
  double d   = (double)deg + min / 60.0;

  if (dir == 'S' || dir == 'W')
    d = -d;
  return d;
}

/**
 * @brief  取 NMEA 语句里第 n 个逗号分隔字段（n 从 1 开始，即第 1 个逗号之后）
 * @note   字段尾遇到 ',' 或 '*'（校验和起始）即结束
 * @retval 1=取到非空字段, 0=字段不存在或为空（dst 置空串）
 */
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen)
{
  const char *p = line;
  uint8_t i = 0;

  dst[0] = '\0';
  while (n > 0) {
    p = strchr(p, ',');
    if (p == NULL)
      return 0;
    p++;
    n--;
  }
  while (*p != '\0' && *p != ',' && *p != '*' && i < dstLen - 1)
    dst[i++] = *p++;
  dst[i] = '\0';

  return (i > 0);
}

/**
 * @brief  解析一条 NMEA 语句并打印
 * @note   支持 GGA（定位质量/经纬度/海拔/卫星数）与 RMC（日期/速度）
 *         $GPxxx / $GNxxx / $GBxxx 三种前缀都认，兼容 GPS/北斗/多模合一模块
 */
static void parseNmeaLine(char *line)
{
  /* 正常运行时完全静默；只有收到 upgps（或手动 sendmode 打开）才打印 */
  if (gpsPrintEn) {
    printf("[GPS] %s\r\n", line);
  }

  /* ---- GGA: 时间、定位质量、经纬度、海拔、卫星数 ---- */
  if (!strncmp(line, "$GPGGA", 6) || !strncmp(line, "$GNGGA", 6) ||
      !strncmp(line, "$GBGGA", 6)) {
    char utc[12], lat[16], latDir[4], lng[16], lngDir[4];
    char qual[4], sats[4], alt[12];

    nmeaField(line, 1, utc, sizeof(utc));
    nmeaField(line, 2, lat, sizeof(lat));
    nmeaField(line, 3, latDir, sizeof(latDir));
    nmeaField(line, 4, lng, sizeof(lng));
    nmeaField(line, 5, lngDir, sizeof(lngDir));
    nmeaField(line, 6, qual, sizeof(qual));
    nmeaField(line, 7, sats, sizeof(sats));
    nmeaField(line, 9, alt, sizeof(alt));

    /* qual: 0=无效, 1=单点定位, 2=差分, 4=RTK固定, 5=RTK浮点 */
    if (atoi(qual) > 0 && lat[0] != '\0' && lng[0] != '\0') {
      gpsFix.valid = 1;
      gpsFix.lat   = nmeaToDeg(lat, latDir[0]);
      gpsFix.lng   = nmeaToDeg(lng, lngDir[0]);
      gpsFix.sats  = (uint8_t)atoi(sats);
      gpsFix.alt   = (float)atof(alt);
      gpsFixValid  = 1;
      gpsFixTick   = HAL_GetTick();   /* 刷新定位时间戳，供新鲜度判断 */

      if (gpsPrintEn) {
        printf("[GPS定位] lat=%.6f lng=%.6f alt=%.1f sats=%d UTC=%s\r\n",
               gpsFix.lat, gpsFix.lng, (double)gpsFix.alt, (int)gpsFix.sats, utc);
      }
    } else {
      gpsFix.valid = 0;
      gpsFixValid  = 0;
      //printf("[GPS] 未定位 (quality=%s sat=%s)\r\n", qual, sats);
    }
  }
  /* ---- RMC: 日期、地面速度（GGA 里没有） ---- */
  else if (!strncmp(line, "$GPRMC", 6) || !strncmp(line, "$GNRMC", 6)) {
    char status[4], spd[12], date[8];

    nmeaField(line, 2, status, sizeof(status));
    nmeaField(line, 7, spd, sizeof(spd));
    nmeaField(line, 9, date, sizeof(date));

    if (status[0] == 'A') {
      gpsFix.speed = (float)(atof(spd) * 1.852);  /* 节 → km/h */
      if (gpsPrintEn) {
        printf("[GPS信息] 日期=%s 速度=%.1f km/h\r\n",
               date, (double)gpsFix.speed);
      }
    }
  }
}

/**
 * @brief  当前是否持有"新鲜"的有效定位（供周期上报判断）
 * @note   光有 gpsFixValid 不够：若模块掉电或天线被遮挡导致不再输出 NMEA，
 *         gpsFixValid 会一直停在 1，从而反复上报过期坐标。
 *         故要求最近 GPS_FIX_TTL_MS 内刷新过一次。
 * @retval 1=可上报定位, 0=应回退到原时间帧
 */
static uint8_t gpsFixFresh(void)
{
  if (!gpsFixValid)
    return 0;
  if (HAL_GetTick() - gpsFixTick > GPS_FIX_TTL_MS)
    return 0;
  return 1;
}

/**
 * @brief  GPS 任务：把环形缓冲里的字节拼成完整语句后解析
 * @note   在主循环里调用；ISR 只负责往环形缓冲塞字节。
 *         语句以 '\n' 结尾，遇到 '\r' 直接丢弃（NMEA 的 CRLF）。
 *         若语句超过 GPS_LINE_LEN 视为异常，丢弃并等待下一个 '\n' 重新同步。
 */
static void gpsTask(void)
{
  while (gpsRd != gpsWr) {
    uint8_t b = gpsRing[gpsRd];
    gpsRd = (gpsRd + 1u) % GPS_RING_LEN;

    if (b == '\n') {
      if (gpsLineLen > 0) {
        gpsLine[gpsLineLen] = '\0';
        parseNmeaLine(gpsLine);
        gpsLineLen = 0;
      }
    } else if (b != '\r') {
      if (gpsLineLen < GPS_LINE_LEN - 1)
        gpsLine[gpsLineLen++] = (char)b;
      else
        gpsLineLen = 0;  /* 超长异常，丢弃重来 */
    }
  }

  /* 长期未定位时给个心跳提示，避免"完全没输出"被误判成串口没收到数据 */
  if (gpsPrintEn && !gpsFixValid && (HAL_GetTick() - gpsLastHint > 10000)) {
    gpsLastHint = HAL_GetTick();
    printf("[GPS] 等待定位... (已收 %lu 字节)\r\n", (unsigned long)gpsRxCount);
  }
}

/**
 * @brief  阻塞等待"新鲜的有效定位"（用于 upgps 指令）
 * @note   此函数运行期间主循环不跑，必须自己反复调用 gpsTask() 排空环形缓冲，
 *         否则永远拿不到新解析结果 —— 这是本函数的核心，不能省
 * @param  minMs:     最短等待时间；即使一进来就有坐标也必须等够这么久
 *                    （给中继留够准备接收的时间，避免发太快丢帧）
 * @param  timeoutMs: 最长等待时间；HAL_GetTick 差值用无符号运算，
 *                    49 天回绕也能正确计算
 * @retval 1=拿到有效定位, 0=超时仍未定位
 */
static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs)
{
  uint32_t startTick = HAL_GetTick();
  uint32_t elapsed;

  printf("[GPS] 开始搜星: 至少 %lu 秒, 上限 %lu 秒...\r\n",
         (unsigned long)(minMs / 1000), (unsigned long)(timeoutMs / 1000));

  while (1) {
    gpsTask();                    /* 排空环形缓冲并解析 NMEA */

    elapsed = HAL_GetTick() - startTick;

    /* 先判"是否已满足上报条件"，再判超时 ——
     * 否则最后一刻刚好定上位，会被超时分支覆盖成失败 */
    if (elapsed >= minMs && gpsFixFresh()) {
      printf("[GPS] 已定位(第 %lu 秒), 已满足最短等待 %lu 秒, 准备上报\r\n",
             (unsigned long)(elapsed / 1000), (unsigned long)(minMs / 1000));
      return 1;
    }

    if (elapsed >= timeoutMs)
      break;

    HAL_Delay(100);
  }

  printf("[GPS] 搜星超时(%lu 秒), 未获得有效定位\r\n",
         (unsigned long)(timeoutMs / 1000));
  return 0;
}

/**
  * @brief  解析接收到的消息（按 '|' 分割字段）
  * @note   格式：MSG_TYPE|字段1|字段2|...|
  */
static void parse_rx_message(uint8_t *buf, uint8_t len)
{
  char *str = (char *)buf;
  char *fields[8] = {0};
  uint8_t field_count = 0;

  fields[field_count++] = str;
  for (uint8_t i = 0; i < len && field_count < 8; i++) {
    if (str[i] == '|') {
      str[i] = '\0';
      if (i + 1 < len)
        fields[field_count++] = &str[i + 1];
    }
  }

  if (field_count < 2) {
    printf("[解析] 无效消息\r\n");
    return;
  }

  uint8_t msg_type = atoi(fields[0]);

  switch (msg_type) {
    case MSG_TYPE_SYN_TIME: /* 4: 下发对时信息 4|timestamp|xxx */
      if (field_count >= 2) {
        uint32_t recvTs = (uint32_t)atoi(fields[1]);
        uint32_t nowTick = HAL_GetTick();

        /* 晶振偏差计算（首次对时设为锚点，后续每次对时输出偏差） */
        if (timeSynced) {
          uint32_t localElapsedMs = nowTick - anchorTick;
          uint32_t localElapsedSec = localElapsedMs / 1000;
          uint32_t remoteElapsedSec = recvTs - anchorTimestamp;
          /* 直接用间隔差值计算偏差，避免绝对时间戳整数截断 */
          int32_t  driftSec = (int32_t)localElapsedSec - (int32_t)remoteElapsedSec;
          int32_t  driftPerHour = (remoteElapsedSec > 0)
              ? (driftSec * 3600 / (int32_t)remoteElapsedSec)
              : 0;
          printf("[晶振] 偏差=%d秒 / %lu秒 => %d秒/小时  本地=%lu 服务器=%lu\r\n",
                 (int)driftSec,
                 (unsigned long)localElapsedSec,
                 (int)driftPerHour,
                 (unsigned long)localElapsedSec,
                 (unsigned long)remoteElapsedSec);
        } else {
          /* 首次对时：记录锚点 */
          anchorTick = nowTick;
          anchorTimestamp = recvTs;
          printf("[晶振] 锚点已设置  ts=%lu tick=%lu\r\n",
                 (unsigned long)recvTs, (unsigned long)nowTick);
        }

        syncedTimestamp = recvTs;
        syncedTick = nowTick;
        timeSynced = 1;
        printf("[对时] ts=%lu 当天秒=%lu\r\n",
               (unsigned long)syncedTimestamp, (unsigned long)getTodaySecond());
        /* 对时成功后重新计算下次上报时间（基于真实时间） */
        calcNextTxTime();
      }
      break;
    case MSG_TYPE_COM: /* 9: 指令  9|deviceId|cmd|value */
      if (field_count >= 3) {
        printf("[指令] 设备=%s, 命令=%s, 值=%s\r\n",
               fields[1], fields[2], field_count > 3 ? fields[3] : "");
        /* realyname: 9|v3-10|realyname|v3-11  →  本机改名 */
        if (strcmp(fields[2], "realyname") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            strncpy(deviceName, fields[3], sizeof(deviceName) - 1);
            deviceName[sizeof(deviceName) - 1] = '\0';
            saveDeviceNameToFlash(deviceName);
            printf("[指令] 设备已改名: %s\r\n", deviceName);
          } else {
            printf("[指令] 改名: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        }
        /* upgps: 9|v0-0|upgps|1  →  等待有效定位后上报一次 GPS 坐标
         * @note  这里只置标志，真正的等待/上报在 OnMaster() 末尾执行：
         *        1) 阻塞搜星：最短 GPS_WAIT_MIN_MS(10秒, 给中继留准备时间),
         *                    上限 GPS_WAIT_TIMEOUT_MS(3分钟)
         *        2) 发 5|deviceName|lat,lng|（MSG_TYPE_UP_GPS，定位失败发 "0.0,0.0"）
         *        3) calcNextTxTime() 重新算下次时间 → 再进深度休眠
         *        不能在中断/解析上下文里长时间等待，否则会堵死整个流程 */
        else if (strcmp(fields[2], "upgps") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            gpsWaitReport = 1;
            /* GPS 打印的开关/恢复统一在 OnMaster() 里做，
             * 这样"指令触发"和"失败自动重试"两条路径行为一致 */
            printf("[指令] upgps: 已收到, 开始搜星(上限 180 秒)\r\n");
          } else {
            printf("[指令] upgps: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        }
        /* sendmode: 9|v0-0|sendmode|1  →  GPS 原始 NMEA 语句打印开关
         *   value=1 : 打印（默认）
         *   value=0 : 不打印
         *   @note  只控制"原始语句转发"这一项；
         *          解析后的 [GPS定位]/[GPS信息] 与未定位心跳仍会打印 */
        else if (strcmp(fields[2], "sendmode") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            int mode = atoi(fields[3]);        /* atoi 会自动跳过前导空格 */
            gpsPrintEn = (mode != 0) ? 1 : 0;  /* 非 0 一律当作开 */
            /* 注意：这里不能用 "开"/"关" 等中文做值。
             * 本文件是无 BOM 的 UTF-8，而 ARMCC5 会按 GBK 去解析多字节：
             * 例如 "关" = E5 85 B3，末尾 B3 落在 GBK 首字节范围 0x81~0xFE，
             * 编译器会把紧随其后的字节当尾字节吞掉 —— 若那正好是闭合引号，
             * 就会报 missing closing quote。故此处刻意用 ASCII。 */
            printf("[指令] GPS原句打印: %s (下发值=%s)\r\n",
                   gpsPrintEn ? "ON" : "OFF", fields[3]);
          } else {
            printf("[指令] sendmode: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        }
        /* txpower: 9|v3-3|txpower|15  →  动态修改发射功率 */
        else if (strcmp(fields[2], "txpower") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            uint8_t newPwr = (uint8_t)atoi(fields[3]);
            currentTxPower = newPwr;
            rf_set_tx_power(newPwr);
            saveTxPowerToFlash(newPwr);
            printf("[指令] 发射功率已设置: %d dBm\r\n", newPwr);
          } else {
            printf("[指令] 功率: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        }
        /* C: 9|v0-0|C|26.52958,109.39087  →  设置农场中心坐标并持久化 */
        else if (strcmp(fields[2], "C") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            double la = 0.0, lo = 0.0;
            /* 用 double 解析，避免 109.39087 这类 8 位有效数字被 float 截断；
             * %lf 会自动跳过 fields[3] 的前导空格（下发值形如 " 26.52,109.39"） */
            if (sscanf(fields[3], "%lf,%lf", &la, &lo) == 2 &&
                centerCoordValid(la, lo)) {
              /* 归一化成 5 位小数存储：帧长固定，且天然不含会破坏帧结构的 '|' */
              snprintf(centerStr, sizeof(centerStr), "%.5f,%.5f", la, lo);
              saveCenterToFlash(centerStr);
              printf("[指令] 中心坐标已保存: %s\r\n", centerStr);
            } else {
              /* 非法：不写内存、不写 Flash，保留上一次的合法值 */
              printf("[指令] C: 坐标非法已拒绝(原值=%s 保持): %s\r\n",
                     (centerStr[0] != '\0') ? centerStr : "(未设置)", fields[3]);
            }
            /* 无论本次是否合法都上报当前坐标：
             * 合法→回新坐标；非法→回上一个合法值（未设过则 "0,0"） */
            centerNeedReport = 1;
          } else {
            printf("[指令] C: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        }
        /* A: 9|v3-17|A|10,0M,3t,6  →  更新配置并标记上报 */
        else if (strcmp(fields[2], "A") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            /* 合法性检查：上报周期必须在 1~60 分钟之间 */
            uint32_t newMin = (uint32_t)atoi(fields[3]);
            if (newMin >= 1 && newMin <= 60) {
              strncpy(configStr, fields[3], sizeof(configStr) - 1);
              configStr[sizeof(configStr) - 1] = '\0';
              saveConfigToFlash(configStr);
              configNeedReport = 1;  /* 标记需要上报配置 */
              printf("[指令] 配置已更新: %s\r\n", configStr);
            } else {
              printf("[指令] A: 间隔=%lu 超范围(1~60), 拒绝\r\n",
                     (unsigned long)newMin);
            }
          } else {
            printf("[指令] A: 目标=%s != 本机=%s, 跳过\r\n",
                   fields[1], deviceName);
          }
        } else {
          printf("[指令] 未知指令: %s\r\n", fields[2]);
        }
      }
      break;
    default:
      printf("[消息] 类型=%d\r\n", msg_type);
      break;
  }
}

/**
  * @brief  Master（主机/发射端）流程
  * @note   1. 组包并发送时间帧 MSG_TYPE_TIME|设备名|当天秒数|电量|计数|
  *            @warning 这里固定发时间帧，不再按定位状态切换！
  *                    定位坐标统一由 UPGPS 流程上报（见末段 gpsSearchAndReport）
  *            例外：若上一轮搜星失败（发了 0.0,0.0），本轮跳过这一帧，
  *                  直接重试搜星，把这次机会留给拿坐标
  *         2. 发送完成后开启 3 秒接收窗口，解析对时消息
  *            @note  接收窗口两种周期都开，保证节点始终可被下发指令
  *         3. 计算下次上报时间并保存到 Flash
  */
static void OnMaster(void)
{
  char tx_msg[TX_LEN];
  uint32_t today_sec = getTodaySecond();
  uint32_t tx_time = 1000;

  /* 计数自增并回绕，取值恒为 1 ~ TX_COUNT_MAX(999)，绝不会出现 0 或 1000 */
  tx_count++;
  if (tx_count > TX_COUNT_MAX)
    tx_count = 1;

  /* 上一轮搜星失败时跳过定时上报，本轮直接重试搜星 */
  if (gpsNeedRetry) {
    printf("[调度] 上轮搜星失败(已上报 0.0,0.0), 本轮跳过定时上报, 重试 UPGPS\r\n");
  } else {
    /* 组包：2|v0-0|当天秒数|99|计数
     * 周期上报固定发时间帧，不管有没有定位 —— 定位由 UPGPS 流程单独触发上报 */
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%lu|%d|%lu",
             MSG_TYPE_TIME, deviceName,
             (unsigned long)today_sec, BATTERY_VAL, (unsigned long)tx_count);

    /* 发送前 LED 闪 3 次，每次 200ms */
    for (uint8_t i = 0; i < 3; i++) {
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
      HAL_Delay(100);
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
      HAL_Delay(100);
    }

    /* 发送数据 */
    if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != RF_OK) {
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }

    /* 等待发送完成标志 */
    while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
      ;
    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    LedToggle();
    printf("发送成功 -> \"%s\"\r\n", tx_msg);
  }

  /* ---- 开启 3 秒接收窗口（single 模式，最多只收一条） ---- */
  rf_enter_single_timeout_rx(3000);
  printf("[接收窗口] 3秒监听中...\r\n");

  /* 等待接收完成或超时 */
  while (rf_get_recv_flag() == RADIO_FLAG_IDLE)
    ;

  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN];
    uint8_t local_size;
    uint8_t local_rssi;
    uint8_t local_snr;

    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    if (local_size < RX_LEN)
      local_buf[local_size] = '\0';
    else
      local_buf[RX_LEN - 1] = '\0';

    printf("[接收] len=%d rssi=%d snr=%d data=%s\r\n",
           local_size, local_rssi - 256, local_snr, (char *)local_buf);

    /* 解析消息 */
    parse_rx_message(local_buf, local_size);
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[接收窗口] 超时, 无数据\r\n");
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[接收窗口] CRC校验错误\r\n");
  }

  /* 配置需要上报时，延迟 1 秒发送配置消息（开机首次或收到配置更新指令） */
  if (configNeedReport) {
    HAL_Delay(1000);
    sendConfigMessage();
    configNeedReport = 0;  /* 标记已上报 */
  }

  /* 收到 C 指令后，延迟 1 秒上报农场中心坐标 1|deviceName|lat,lng| */
  if (centerNeedReport) {
    HAL_Delay(1000);
    sendCenterMessage();
    centerNeedReport = 0;  /* 标记已上报 */
  }

  /* UPGPS 流程：搜星并上报一次实时坐标。
   * 触发来源有二：(1) 本轮收到 upgps 指令；(2) 上轮搜星失败自动重试。
   * 放在 calcNextTxTime() 之前 —— 等待可能耗时几十秒，
   * 之后必须重新计算下次上报时间，不能用本轮算出的旧值。 */
  if (gpsWaitReport || gpsNeedRetry) {
    uint8_t hasFix;

    gpsWaitReport = 0;  /* 先清零，避免异常路径残留导致每轮都在这儿等 */

    /* 搜星期间打开 GPS 打印便于观察，gpsSearchAndReport() 内部负责恢复 */
    gpsPrintSaved = gpsPrintEn;
    gpsPrintEn = 1;

    hasFix = gpsSearchAndReport();

    /* 记录本轮结果：失败则下轮继续重试(跳过定时上报)，成功则清零恢复正常周期 */
    gpsNeedRetry = hasFix ? 0U : 1U;

    /* 注意：分支提示不要写成 one-liner 三元 + 中文值，
     * 中文紧贴闭合引号会被 ARMCC5 按 GBK 吞掉那个引号（missing closing quote），
     * 故改成 if/else + 以 \r\n 收尾（与文件其它 printf 风格一致） */
    if (gpsNeedRetry)
      printf("[调度] 下轮继续重试搜星(跳过定时上报)\r\n");
    else
      printf("[调度] 下轮恢复正常定时上报\r\n");
  }

  /* 计算下次上报时间（配置更新后间隔可能变化） */
  calcNextTxTime();

  /* 判断是否需要进入深度休眠 */
  {
    uint32_t nowTs = getCurrentTimestamp();
    uint32_t waitSec = (nextTxTimestamp > nowTs) ? (nextTxTimestamp - nowTs) : 0;
    if (waitSec > 10) {
      /* 下次上报 > 10 秒后 → PAN3029 进入深度休眠省电 */
      printf("[休眠] 下次发送 %lu 秒后, LoRa进入深度休眠...\r\n", (unsigned long)waitSec);
      rf_deepsleep();
      loraInitialized = 0;  /* 标记需要唤醒后重新初始化 */
    } else {
      printf("[活跃] 下次发送 %lu 秒后, LoRa保持就绪\r\n", (unsigned long)waitSec);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  应用程序主入口函数
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有配置的外设 */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_NVIC_Init();

  /* USER CODE BEGIN 2 */

  /* 从 Flash 加载设备名、配置、发射功率和农场中心坐标 */
  loadDeviceNameFromFlash();
  loadConfigFromFlash();
  loadTxPowerFromFlash();
  loadCenterFromFlash();

  /* 打印启动信息 */
  printf("\r\n===== LoRa 仅发送模式启动 =====\r\n");
  printf_logo();
  printf("设备: %s\r\n", deviceName);
  printf("配置: %s\r\n", configStr);
  printf("时隙  : 序号=%d, 间隔=%lu秒, 偏移=%lu秒\r\n",
         getDeviceIndex(),
         (unsigned long)getConfigIntervalSec(),
         (unsigned long)getSlotOffsetSec());

  /* 首次 TX 延迟到主循环中执行（LoRa 在首次 TX 前才初始化） */
  printf("TX模式: 仅发送\r\n");
  nextTxTimestamp = 0;  /* 首次立即触发 TX */
  printf("[调度] 就绪, 首次立即发送...\r\n");

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */
    uint32_t nowTs = getCurrentTimestamp();

    if (loraInitialized) {
      /* LoRa 已激活：检查是否到达发送时间 */
      if (nowTs >= nextTxTimestamp) {
        OnMaster();  /* 内部会处理发送、接收窗口、以及发送后的休眠判断 */
      }
    } else {
      /* LoRa 未初始化（首次启动 或 从 deepsleep 唤醒） */
      uint32_t wakeTime = (nextTxTimestamp > 5) ? (nextTxTimestamp - 5) : 0;
      if (nowTs >= wakeTime) {
        /* 唤醒 LoRa 并执行 5 秒倒计时 */
        printf("[WAKE] LoRa 唤醒中, 5秒倒计时...\r\n");
        loraInitFirstTime();
        for (int i = 5; i > 0; i--) {
          char time_str[20];
          formatTimestamp(getCurrentTimestamp(), time_str, sizeof(time_str));
          printf("[WAKE] %s  %d\r\n", time_str, i);
          HAL_Delay(1000);
        }
      }
    }

    /* GPS: 取出环形缓冲里的 NMEA 语句并打印/解析 */
    gpsTask();

    HAL_Delay(100);

    /* USER CODE END 3 */
  }
  /* USER CODE END WHILE */
}

/**
  * @brief System Clock Configuration (72MHz)
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
  * @brief USART1 Initialization Function (调试串口 115200)
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
  * @brief USART3 Initialization Function (GPS 模块 9600, 仅接收)
  * @note  PB11 = USART3_RX（PB10/TX 由 HAL_UART_MspInit 一并配置，但本模式不用）
  *        中断优先级设为 2，低于 LoRa(EXTI2=0)，避免影响射频时序
  */
static void MX_USART3_UART_Init(void)
{
  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);

  /* 开启 GPS 逐字节中断接收 */
  HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);

  /* USER CODE END USART3_Init 2 */
}

/**
  * @brief  GPIO 初始化函数
  * @note   配置说明：
  *         1. LED 指示灯：LED1(PB15)
  *         2. LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1
  *         3. LoRa 控制引脚：CSN(PB0), RST(PA1)
  *         4. LoRa 中断引脚：IRQ(PA2) - 上升沿触发，下拉
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* LED1 (PB15) 默认灭 */
  HAL_GPIO_WritePin(GPIOB, LED1_Pin, GPIO_PIN_RESET);

  /* 配置 LED1 引脚 (PB15) */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LoRa CSN 片选引脚 (PB0) - 默认高电平 */
  GPIO_InitStruct.Pin  = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);

  /* LoRa RST 复位引脚 (PA1) - 默认高电平 */
  GPIO_InitStruct.Pin  = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);

  /* LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
  * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI）
  * @note   CPOL=0, CPHA=0, MSB, 软件 NSS, 波特率 4.5MHz
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  NVIC 中断控制器初始化（LoRa IRQ EXTI2）
  */
static void MX_NVIC_Init(void)
{
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/* USER CODE BEGIN 4 */

/**
  * @brief  GPIO 外部中断回调函数
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* LoRa IRQ 中断: PA2 */
    if (GPIO_Pin == GPIO_PIN_2)
    {
        rf_irq_process();
    }
}

/**
 * @brief  USART3 接收完成回调（GPS 逐字节接收）
 * @note   只做两件最轻的动作：入环形缓冲 + 重新开启下一字节接收。
 *         绝不在 ISR 里做字符串解析或 printf —— 那样会长时间占用中断。
 *         缓冲满时丢弃本次字节，下一条语句的 '\n' 会自动重新同步。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    uint16_t next = (gpsWr + 1u) % GPS_RING_LEN;

    if (next != gpsRd)          /* 环形缓冲未满 */
    {
      gpsRing[gpsWr] = gpsByte;
      gpsWr = next;
      gpsRxCount++;
    }

    HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);
  }
}

/* USER CODE END 4 */

/**
  * @brief  错误处理函数
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
