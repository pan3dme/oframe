/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 随机间隔测试发送节点 - STM32F103C8T6
  * @note           功能说明（简单测试固件）：
  *                 1. 开机初始化 LoRa，GPS 一次性上电并保持常开（不复电不复位）
  *                 2. 每 10 秒上报一帧 type=5 GPS 坐标（NMEA 解析参考 ONLY_SEND）
  *                 3. 发射功率仍按 10~20 dBm 逐帧循环（保留原测试功能）
  *                 4. 只发不收（LoRa 侧），无 RTC 休眠 / 对时 / 指令解析
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

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TX_LEN 64

#define TX_INTERVAL_SEC     10     /* 固定发送间隔（秒） */
#define TX_PWR_MIN          10     /* 发射功率循环下限（dBm） */
#define TX_PWR_MAX          20     /* 发射功率循环上限（dBm） */

#define MSG_TYPE_UP_GPS     5      /* GPS 上传帧类型（与 ONLY_SEND 一致） */
#define GPS_RING_LEN        512    /* GPS 串口环形缓冲 */
#define GPS_LINE_LEN        100    /* NMEA 单行缓冲 */
#define GPS_PWR_ON_DELAY_MS 1000   /* GPS 上电后等待模块起振 (ms) */
/* TEST_MODE selects the test scenario:
 *   0 = time/sleep test (no LoRa, no GPS: print uptime for 1 min,
 *       deep sleep 1 min via STOP + RTC alarm, alternate forever)
 *   1 = GPS TX test (original firmware) */
#define TEST_MODE 0
#define ACTIVE_MS  60000u   /* active window: print time (1 min) */
#define SLEEP_SEC  60u      /* deep sleep (STOP + RTC alarm) (1 min) */

#if TEST_MODE == 0
#pragma diag_suppress 177,550  /* mode-1 helpers stay compiled but unreferenced */
#endif
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;    /* LoRa PAN3029 */
UART_HandleTypeDef huart1;  /* 调试打印 */
UART_HandleTypeDef huart3;  /* 仅保留定义：stm32f1xx_it.c 的 USART3 中断引用 */

/* USER CODE BEGIN PV */
static uint32_t txCount = 0;   /* 发送计数，随帧发出便于核对丢帧 */

/* --- GPS 接收/解析（参考 ONLY_SEND，精简版） --- */
static volatile uint8_t  gpsRing[GPS_RING_LEN];
static volatile uint16_t gpsWr = 0, gpsRd = 0;
static uint8_t  gpsByte;
static char     gpsLine[GPS_LINE_LEN];
static uint16_t gpsLineLen = 0;
static uint32_t gpsRxCount = 0;          /* 收到字节总数（诊断用） */

typedef struct {
  int32_t lat;    /* lat × 1e5 */
  int32_t lng;    /* lng × 1e5 */
  uint8_t sats;
} GpsFix_t;

static GpsFix_t gpsFix;
static uint8_t  gpsFixValid = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */
static void loraInitFirstTime(void);
static void loraTx(const char *msg);
static void MX_USART3_UART_Init(void);
static void gpsTask(void);
static void parseNmeaLine(char *line);
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen);
static int32_t nmeaToDeg(const char *v, char dir);
static void deg1e5ToStr(int32_t v, char *buf);
static void fmtRxBytes(uint32_t bytes, char *buf, uint8_t len);
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
  * @brief  首次初始化 LoRa 模块（从关机状态启动）
  * @note   初始化失败：给 RST 低脉冲 → 3 秒后软件复位 MCU 重跑初始化
  */
static void loraInitFirstTime(void)
{
  printf("LoRa init...\r\n");

  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  if (rf_init() != RF_OK) {
    printf("RF init FAIL! reset MCU in 3s\r\n");

    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(3000);

    printf("[RESET] NVIC_SystemReset() ...\r\n");
    NVIC_SystemReset();              /* noreturn，不会回来 */
  }
  printf("RF init OK\r\n");

  rf_set_default_para();

  printf("[LoRa] ready: %d MHz SF%d BW125 CR4/5 CRC-off SW0x12\r\n",
         DEFAULT_FREQ / 1000000, DEFAULT_SF);
}

/**
  * @brief  LoRa 单发一帧（阻塞等发送完成）
  * @param  msg: 以 '\0' 结尾的帧字符串（连同结束符一起发送）
  */
static void loraTx(const char *msg)
{
  uint32_t tx_time = 1000;

  rf_single_tx_data((uint8_t *)msg, strlen(msg) + 1, &tx_time);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
}

/* =====================================================================
 * GPS 接收与 NMEA 解析（参考 ONLY_SEND，精简版：只解析 GGA 坐标）
 * ===================================================================== */

/**
 * @brief  从 NMEA 行取第 n 个逗号字段（n 从 1 起），截断写入 dst
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
 * @brief  ddmm.mmmmm 经纬度 -> 度 ×1e5 整数；畸形输入一律返回 0
 * @note   整数部分 >4 位 = 乱码行，拒绝（防 int32 溢出）
 */
static int32_t nmeaToDeg(const char *v, char dir)
{
  const char *dot = strchr(v, '.');
  int32_t    deg = 0;
  uint32_t   minE4 = 0;   /* 分钟 ×1e4，如 29.58 -> 295800 */
  uint32_t   mul = 100000;
  int32_t    d;
  uint8_t    nd = 0;

  if (dot == NULL || dot - v < 3)
    return 0;

  while (v < dot - 2) {
    if (*v < '0' || *v > '9' || ++nd > 4)
      return 0;
    deg = deg * 10 + (*v++ - '0');
  }
  v = dot - 2;          /* 分钟整数 2 位 */
  for (d = 0; d < 2; d++) {
    if (*v < '0' || *v > '9')
      return 0;
    minE4 += (uint32_t)(*v++ - '0') * mul;
    mul /= 10;
  }
  if (*v == '.')
    v++;
  while (mul && *v >= '0' && *v <= '9') {
    minE4 += (uint32_t)(*v++ - '0') * mul;
    mul /= 10;
  }

  d = (int32_t)deg * 100000 + (int32_t)((minE4 + 3) / 6);
  if (dir == 'S' || dir == 'W')
    d = -d;
  return d;
}

/**
 * @brief  度×1e5 -> "[-]dd.ddddd"（等价 %.5f）
 */
static void deg1e5ToStr(int32_t v, char *buf)
{
  uint32_t a = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;

  if (v < 0)
    *buf++ = '-';
  snprintf(buf, 14, "%lu.%05lu",
           (unsigned long)(a / 100000), (unsigned long)(a % 100000));
}

/**
 * @brief  串口字节数转紧凑可读格式（与 ONLY_SEND 无定位帧第 4 段一致）
 * @note   <1024 -> "NByte"；<1M -> "X.XXK"；>=1M -> "X.XXM"，
 *         两位小数用整数运算（×100/1024），不引入浮点库
 */
static void fmtRxBytes(uint32_t bytes, char *buf, uint8_t len)
{
  if (bytes < 1024u) {
    snprintf(buf, len, "%luByte", (unsigned long)bytes);
  } else if (bytes < 1024u * 1024u) {
    snprintf(buf, len, "%lu.%02luK",
             (unsigned long)(bytes / 1024u),
             (unsigned long)((bytes % 1024u) * 100u / 1024u));
  } else {
    snprintf(buf, len, "%lu.%02luM",
             (unsigned long)(bytes / (1024u * 1024u)),
             (unsigned long)((bytes % (1024u * 1024u)) * 100u / (1024u * 1024u)));
  }
}

/**
 * @brief  解析一条 NMEA 行（只认 GGA：$GP/$GN/$GB 前缀）
 */
static void parseNmeaLine(char *line)
{
  if (!strncmp(line, "$GPGGA", 6) || !strncmp(line, "$GNGGA", 6) ||
      !strncmp(line, "$GBGGA", 6)) {
    char lat[16], latDir[4], lng[16], lngDir[4];
    char qual[4], sats[4];

    nmeaField(line, 2, lat, sizeof(lat));
    nmeaField(line, 3, latDir, sizeof(latDir));
    nmeaField(line, 4, lng, sizeof(lng));
    nmeaField(line, 5, lngDir, sizeof(lngDir));
    nmeaField(line, 6, qual, sizeof(qual));
    nmeaField(line, 7, sats, sizeof(sats));

    /* qual: 0=无效, 1=单点, 2=差分, 4/5=RTK */
    if (atoi(qual) > 0 && lat[0] != '\0' && lng[0] != '\0') {
      gpsFix.lat   = nmeaToDeg(lat, latDir[0]);
      gpsFix.lng   = nmeaToDeg(lng, lngDir[0]);
      gpsFix.sats  = (uint8_t)atoi(sats);
      if (gpsFix.lat != 0 || gpsFix.lng != 0)
        gpsFixValid = 1;
    }
  }
}

/**
 * @brief  排空 GPS 环形缓冲并按行解析（主循环每 100ms 调一次）
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
        gpsLineLen = 0;  /* 超长异常，丢弃重同步 */
    }
  }
}

/**
 * @brief  USART3 初始化（GPS 模块 9600，仅接收 PB11）
 */
static void MX_USART3_UART_Init(void)
{
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

  HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);

  /* 开 GPS 逐字节中断接收 */
  HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
#if TEST_MODE == 0
/* =====================================================================
 * Time/sleep test support: RTC on LSI (40kHz/40000 = 1Hz), alarm wakes
 * STOP via EXTI17 event (WFE, NVIC not used - RTC_Alarm_IRQHandler in
 * stm32f1xx_it.c stays as a fallback). GPIO states are frozen in STOP,
 * so GPS stays powered off for the whole sleep.
 * ===================================================================== */
static uint32_t rtcCnt(void)
{
  uint16_t h, l, h2;

  do {
    h  = RTC->CNTH;
    l  = RTC->CNTL;
    h2 = RTC->CNTH;
  } while (h != h2);
  return ((uint32_t)h << 16) | l;
}

/* Force RTC source to LSI: a stale backup-domain selection (e.g. LSE left
 * by another firmware, or dead crystal) would freeze the counter and the
 * alarm would never fire - board would sleep forever. BDRST wipes it. */
static void rtcInitLsi(void)
{
  uint32_t guard;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  PWR->CR |= PWR_CR_DBP;

  if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_LSI) {
    RCC->BDCR |= RCC_BDCR_BDRST;
    RCC->BDCR &= (uint32_t)~RCC_BDCR_BDRST;
    RCC->CSR |= RCC_CSR_LSION;
    guard = 2000000u;
    while (!(RCC->CSR & RCC_CSR_LSIRDY) && --guard)
      ;
    if (!guard) {
      printf("[RTC] LSI FAIL, reset\r\n");
      NVIC_SystemReset();
    }
    RCC->BDCR |= RCC_BDCR_RTCSEL_LSI | RCC_BDCR_RTCEN;
  }

  /* 1Hz prescaler (harmless rewrite every boot) */
  while (!(RTC->CRL & RTC_CRL_RTOFF))
    ;
  RTC->CRL |= RTC_CRL_CNF;
  RTC->PRLH = 0u;
  RTC->PRLL = 39999u;
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  while (!(RTC->CRL & RTC_CRL_RTOFF))
    ;
}

static uint32_t totalSec = 0;   /* uptime seconds (RAM survives STOP) */

/* Enter STOP (low-power regulator), wake on RTC alarm via EXTI17 event.
 * GPIO states are frozen in STOP: GPS stays off, LoRa stays in reset. */
static void rtcSleepSec(uint32_t sec)
{
  uint32_t alarm = rtcCnt() + sec;

  EXTI->IMR  |= EXTI_IMR_MR17;
  EXTI->EMR  |= EXTI_EMR_MR17;
  EXTI->RTSR |= EXTI_RTSR_TR17;
  EXTI->FTSR &= (uint32_t)~EXTI_FTSR_TR17;
  EXTI->PR   = EXTI_PR_PR17;               /* clear stale pending */
  RTC->CRL  &= (uint16_t)~RTC_CRL_ALRF;    /* clear stale alarm flag */

  while (!(RTC->CRL & RTC_CRL_RTOFF))
    ;
  RTC->CRL |= RTC_CRL_CNF;
  RTC->ALRH = (uint16_t)(alarm >> 16);
  RTC->ALRL = (uint16_t)alarm;
  RTC->CRH |= RTC_CRH_ALRIE;
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  while (!(RTC->CRL & RTC_CRL_RTOFF))
    ;

  printf("[SLEEP] STOP, wake in %lus\r\n", (unsigned long)sec);
  PWR->CR |= PWR_CR_LPDS;                  /* low-power regulator in STOP */
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
  __SEV();                                 /* stale-event race guard */
  __WFE();
  __WFE();                                 /* actual STOP here */
  SCB->SCR &= (uint32_t)~SCB_SCR_SLEEPDEEP_Msk;

  /* woke: clear alarm leftovers, restore 72MHz (STOP wakes at HSI 8MHz) */
  while (!(RTC->CRL & RTC_CRL_RTOFF))
    ;
  RTC->CRH &= (uint16_t)~RTC_CRH_ALRIE;
  RTC->CRL &= (uint16_t)~RTC_CRL_ALRF;
  EXTI->PR = EXTI_PR_PR17;
  SystemClock_Config();
}
#endif /* TEST_MODE == 0 */

int main(void)
{
  /* MCU 配置 --------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* 初始化外设：GPIO(GPS 一次性上电) + SPI1 + printf 串口 + GPS 串口 */
  MX_GPIO_Init();
  MX_NVIC_Init();
  MX_SPI1_Init();
#if TEST_MODE == 0
  /* GPS off: PB15 high (Vcc off), PB1 low (standby), PA3 high (no reset) */
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET);
  /* hold LoRa chip in reset: minimize its quiescent current */
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);

  rtcInitLsi();
#endif
  MX_USART1_UART_Init();
  HAL_Delay(GPS_PWR_ON_DELAY_MS);   /* 等 GPS 模块上电起振 */
  MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */

#if TEST_MODE == 1
  loraInitFirstTime();

  printf("===== LoRa GPS test TX =====\r\n");
  printf("interval: %d s fixed, type=%d frame, tx power %d~%d dBm cycling\r\n",
         (int)TX_INTERVAL_SEC, (int)MSG_TYPE_UP_GPS,
         (int)TX_PWR_MIN, (int)TX_PWR_MAX);
#else
  printf("===== time sleep test: no LoRa, no GPS =====\r\n");
  printf("active %lus (print time) <-> deep sleep %lus, alternating\r\n",
         (unsigned long)(ACTIVE_MS / 1000u), (unsigned long)SLEEP_SEC);
#endif
  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */
#if TEST_MODE == 1
    static uint8_t txPwr = TX_PWR_MIN;   /* 当前发射功率，逐帧递增循环 */
    uint32_t waited = 0;
    char tx_msg[TX_LEN];

    /* 固定间隔倒计时；期间每 100ms 跑一次 gpsTask 排空 GPS 环形缓冲
     * （9600bps 下每 100ms 约 96 字节，512 字节缓冲充裕） */
    while (waited < TX_INTERVAL_SEC * 1000u) {
      gpsTask();
      HAL_Delay(100);
      waited += 100;
      if (waited % 1000u == 0u) {
        printf("[Countdown] %lus%s\r\n",
               (unsigned long)((TX_INTERVAL_SEC * 1000u - waited) / 1000u),
               gpsFixValid ? " [FIX]" : "");
      }
    }

    /* 每帧先切功率再发送：功率仍逐帧循环，帧为 type=5 GPS 坐标 */
    gpsTask();
    rf_set_tx_power(txPwr);
    txCount++;
    if (gpsFixValid) {
      char latBuf[16], lngBuf[16];

      deg1e5ToStr(gpsFix.lat, latBuf);
      deg1e5ToStr(gpsFix.lng, lngBuf);
      snprintf(tx_msg, sizeof(tx_msg), "%d|v3-88|%s,%s|%u|%lu|",
               (int)MSG_TYPE_UP_GPS, latBuf, lngBuf,
               (unsigned)gpsFix.sats, (unsigned long)txCount);
    } else {
      /* 无定位：第 4 段 = 累计串口字节数（ONLY_SEND 同款格式），
       * 诊断"模块没吐数(0Byte)"还是"有输出没定住(数值增长)" */
      char bytesBuf[12];

      fmtRxBytes(gpsRxCount, bytesBuf, sizeof(bytesBuf));
      snprintf(tx_msg, sizeof(tx_msg), "%d|v3-88|0.0,0.0|%s|%lu|",
               (int)MSG_TYPE_UP_GPS, bytesBuf, (unsigned long)txCount);
    }
    loraTx(tx_msg);
    printf("TX OK pwr=%d fix=%d -> \"%s\"\r\n",
           (int)txPwr, (int)gpsFixValid, tx_msg);

    txPwr++;
    if (txPwr > TX_PWR_MAX)
      txPwr = TX_PWR_MIN;
#else
    /* time/sleep test: print uptime every second for 1 min,
     * then deep sleep 1 min, alternate */
    {
      uint32_t start = HAL_GetTick();
      uint32_t last = 0xFFFFFFFFu;

      while ((HAL_GetTick() - start) < ACTIVE_MS) {
        uint32_t el = (HAL_GetTick() - start) / 1000u;

        if (el != last) {
          last = el;
          printf("[T] up %02lu:%02lu\r\n",
                 (unsigned long)((totalSec + el) / 60u),
                 (unsigned long)((totalSec + el) % 60u));
        }
      }
      totalSec += ACTIVE_MS / 1000u;
      rtcSleepSec(SLEEP_SEC);
      totalSec += SLEEP_SEC;
      printf("[WAKE] clock 72MHz restored, total up %lus\r\n",
             (unsigned long)totalSec);
    }
#endif
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
}

/**
  * @brief  GPIO 初始化函数
  * @note   配置说明：
  *         1. LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1
  *         2. LoRa 控制引脚：CSN(PB0), RST(PA1)
  *         3. GPS 电源/唤醒/复位引脚保持"断电"初始态（本固件不用 GPS）
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* PB15 = GPS 电源：低电平 = 上电（本固件 GPS 一次性开机，之后保持常开） */
  GPIO_InitStruct.Pin   = GPS_PWR_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_PWR_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_RESET);

  /* GPS 复位引脚 (PA3) - 高电平(不复位) */
  GPIO_InitStruct.Pin   = GPS_RST_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_RST_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET);

  /* GNSS 唤醒脚 (PB1) - 高 = Full on（GPS 常开工作态） */
  GPIO_InitStruct.Pin   = GNSS_WAKE_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GNSS_WAKE_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_SET);

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

  /* LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉
   * @note TX 完成标志由 rf_irq_process() 在本中断里置位，缺了它发送会卡死 */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
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
  * @note   TX 完成标志靠 EXTI2 中断里的 rf_irq_process() 置位
  */
static void MX_NVIC_Init(void)
{
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/**
  * @brief  GPIO 外部中断回调函数（LoRa IRQ: PA2）
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_2)
  {
    rf_irq_process();
  }
}

/**
 * @brief  USART3 接收完成回调（GPS 逐字节入环形缓冲）
 * @note   ISR 只动写指针、gpsTask 只动读指针（单生产者单消费者）；
 *         缓冲满时丢弃本字节，下条语句的 '\n' 自动重新同步
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

/**
 * @brief  UART 错误回调（GPS 串口出错自愈）
 * @note   GPS 上电瞬间的波形毛刺/EMI 会触发 ORE/NE/FE 错误，HAL 会中断
 *         接收；不重新武装的话 GPS 此后永远无数据
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);
  }
}

/**
  * @brief  错误处理函数
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
