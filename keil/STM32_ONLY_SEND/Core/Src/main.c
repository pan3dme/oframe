/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 定时上报节点 - STM32F103C8T6
  *                   移植自 PAN3029_F030 上报逻辑
  * @note           功能说明：
  *                 1. TX 模式：每隔 3 分钟上报一帧（MSG_TYPE_TIME|设备名|当天秒数|电量|计数）
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

/* Flash 存储地址（STM32F103C8T6 64KB） */
#define DEVNAME_FLASH_ADDR  0x0800F800  /* 设备名存储（倒数第二页） */
#define DEVNAME_MAGIC       0xA5A55A5B  /* 修改魔术字使旧 Flash 数据失效 */
#define CONFIG_FLASH_ADDR   0x0800F400  /* 配置字符串存储（倒数第三页） */
#define CONFIG_MAGIC        0x5A5AA5A6  /* 修改魔术字使旧 Flash 数据失效 */
#define TXPWR_FLASH_ADDR    0x0800F000  /* 发射功率存储（倒数第四页） */
#define TXPWR_MAGIC         0xA5A55A5C  /* 功率魔术字 */

/* 上报间隔：由 configStr 第一个字段决定（分钟），如 "30,0M,3t,6" = 30 分钟 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;
I2C_HandleTypeDef hi2c1;  /* OLED 模块需要 */

/* USER CODE BEGIN PV */

/* 时间同步变量 */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间戳 */
static uint32_t syncedTick = 0;       /* 同步时的 HAL_GetTick() 值 */
static uint8_t  timeSynced = 0;       /* 是否已同步时间 */
static uint8_t  configNeedReport = 1;  /* 配置需要上报（开机=1，收到指令更新=1，上报后=0） */
static uint32_t nextTxTimestamp = 0;   /* 下次上报的绝对时间戳（秒） */
static uint8_t  currentTxPower = DEFAULT_PWR;  /* 当前发射功率（从 Flash 加载或指令修改） */

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
static void sendConfigMessage(void);
static uint32_t getConfigIntervalSec(void);
static uint8_t  getDeviceIndex(void);
static uint32_t getSlotOffsetSec(void);
static void calcNextTxTime(void);
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
    printf("[Flash] DeviceName loaded: %s\r\n", deviceName);
  } else {
    printf("[Flash] No saved name, use default: %s\r\n", deviceName);
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
  printf("[Flash] DeviceName saved: %s\r\n", name);
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
    printf("[Flash] Config loaded: %s\r\n", configStr);
  } else {
    printf("[Flash] No saved config, use default: %s\r\n", configStr);
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
  printf("[Flash] Config saved: %s\r\n", str);
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
      printf("[Flash] TxPower loaded: %d dBm\r\n", currentTxPower);
    } else {
      printf("[Flash] TxPower out of range (%d), use default: %d\r\n",
             saved, DEFAULT_PWR);
    }
  } else {
    printf("[Flash] No saved TxPower, use default: %d dBm\r\n", DEFAULT_PWR);
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
  printf("[Flash] TxPower saved: %d dBm\r\n", power);
}

/**
  * @brief  发送配置消息（重启后只发一次）
  * @note   格式: MSG_TYPE_CONFIG|deviceName|config_str
  */
static void sendConfigMessage(void)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;

  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|",
           MSG_TYPE_CONFIG, deviceName, configStr);

  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != RF_OK) {
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
  }

  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  LedToggle();
  printf("[CONFIG] tx ok -> \"%s\"\r\n", tx_msg);
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
    printf("[parse] invalid msg\r\n");
    return;
  }

  uint8_t msg_type = atoi(fields[0]);

  switch (msg_type) {
    case MSG_TYPE_SYN_TIME: /* 4: 下发对时信息 4|timestamp|xxx */
      if (field_count >= 2) {
        syncedTimestamp = atoi(fields[1]);
        syncedTick = HAL_GetTick();
        timeSynced = 1;
        printf("[SYNC] ts=%lu today=%lu\r\n",
               (unsigned long)syncedTimestamp, (unsigned long)getTodaySecond());
        /* 对时成功后重新计算下次上报时间（基于真实时间） */
        calcNextTxTime();
      }
      break;
    case MSG_TYPE_COM: /* 9: 指令  9|deviceId|cmd|value */
      if (field_count >= 3) {
        printf("[CMD] devId=%s, cmd=%s, val=%s\r\n",
               fields[1], fields[2], field_count > 3 ? fields[3] : "");
        /* realyname: 9|v3-10|realyname|v3-11  →  本机改名 */
        if (strcmp(fields[2], "realyname") == 0 && field_count >= 4) {
          if (strcmp(fields[1], deviceName) == 0) {
            strncpy(deviceName, fields[3], sizeof(deviceName) - 1);
            deviceName[sizeof(deviceName) - 1] = '\0';
            saveDeviceNameToFlash(deviceName);
            printf("[CMD] Device renamed to: %s\r\n", deviceName);
          } else {
            printf("[CMD] realyname: target=%s != local=%s, skip\r\n",
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
            printf("[CMD] TxPower set to: %d dBm\r\n", newPwr);
          } else {
            printf("[CMD] txpower: target=%s != local=%s, skip\r\n",
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
              printf("[CMD] Config updated: %s\r\n", configStr);
            } else {
              printf("[CMD] A: interval=%lu out of range (1~60), reject\r\n",
                     (unsigned long)newMin);
            }
          } else {
            printf("[CMD] A: target=%s != local=%s, skip\r\n",
                   fields[1], deviceName);
          }
        } else {
          printf("[CMD] unknown cmd: %s\r\n", fields[2]);
        }
      }
      break;
    default:
      printf("[MSG] type=%d\r\n", msg_type);
      break;
  }
}

/**
  * @brief  Master（主机/发射端）流程
  * @note   1. 组包并发送（MSG_TYPE_TIME|设备名|当天秒数|电量|计数）
  *         2. 发送完成后开启 3 秒接收窗口，解析对时消息
  *         3. 计算下次上报时间并保存到 Flash
  */
static void OnMaster(void)
{
  static uint32_t tx_count = 0;
  char tx_msg[TX_LEN];
  uint32_t today_sec = getTodaySecond();
  uint32_t tx_time = 1000;

  tx_count++;

  /* 组包：2|v4-18|当天秒数|100|计数 */
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
  printf("tx ok -> \"%s\"\r\n", tx_msg);

  /* ---- 开启 3 秒接收窗口（single 模式，最多只收一条） ---- */
  rf_enter_single_timeout_rx(3000);
  printf("[RX window] 3s listening...\r\n");

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

    printf("[RX] len=%d rssi=%d snr=%d data=%s\r\n",
           local_size, local_rssi - 256, local_snr, (char *)local_buf);

    /* 解析消息 */
    parse_rx_message(local_buf, local_size);
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[RX window] timeout, no data.\r\n");
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[RX window] crc error.\r\n");
  }

  /* 配置需要上报时，延迟 1 秒发送配置消息（开机首次或收到配置更新指令） */
  if (configNeedReport) {
    HAL_Delay(1000);
    sendConfigMessage();
    configNeedReport = 0;  /* 标记已上报 */
  }

  /* 计算下次上报时间（配置更新后间隔可能变化） */
  calcNextTxTime();
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
  MX_NVIC_Init();

  /* USER CODE BEGIN 2 */

  /* 从 Flash 加载设备名、配置和发射功率 */
  loadDeviceNameFromFlash();
  loadConfigFromFlash();
  loadTxPowerFromFlash();

  /* 打印启动信息 */
  printf("\r\n===== LoRa Only Send Start =====\r\n");
  printf_logo();
  printf("Device: %s\r\n", deviceName);
  printf("Config: %s\r\n", configStr);
  printf("Slot  : index=%d, interval=%lu s, offset=%lu s\r\n",
         getDeviceIndex(),
         (unsigned long)getConfigIntervalSec(),
         (unsigned long)getSlotOffsetSec());

  HAL_Delay(1);

  /* LoRa PAN3029 模块初始化 */
  printf("LoRa init...\r\n");

  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  if (rf_init() != RF_OK) {
    printf("RF init fail! Check wiring.\r\n");
    while (1) {
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
      HAL_Delay(100);
    }
  }
  printf("RF init ok\r\n");

  rf_set_default_para();
  rf_set_tx_power(currentTxPower);  /* 应用持久化的发射功率 */

  printf("===== LoRa Ready =====\r\n");
  printf("  Freq    : %d MHz\r\n", DEFAULT_FREQ / 1000000);
  printf("  SF      : %d\r\n", DEFAULT_SF);
  printf("  BW      : 125 kHz\r\n");
  printf("  CR      : 4/5\r\n");
  printf("  Power   : %d dBm (current)\r\n", currentTxPower);
  printf("  CRC     : OFF\r\n");
  printf("  SyncWord: 0x12\r\n");
  printf("----------------------------\r\n");

  /* 默认进入 TX 模式（与 PAN3029_F030 一致） */
  printf("TX mode: transmit only.\r\n");

  /* 开机 5 秒倒计时（未对时，时间为 1970 起始） */
  printf("[BOOT] 5s countdown (time from 1970)...\r\n");
  for (int i = 5; i > 0; i--) {
    char time_str[20];
    formatTimestamp(getCurrentTimestamp(), time_str, sizeof(time_str));
    printf("[BOOT] %s  %d\r\n", time_str, i);
    HAL_Delay(1000);
  }

  /* 倒计时结束后立即上报第一次（nextTx=0，主循环马上触发） */
  nextTxTimestamp = 0;
  printf("[SCHED] Ready, first TX immediately...\r\n");

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */

    /* 检查是否到达上报时间 */
    if (getCurrentTimestamp() >= nextTxTimestamp) {
      OnMaster();
    }

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
