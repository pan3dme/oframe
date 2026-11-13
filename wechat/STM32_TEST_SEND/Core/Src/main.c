/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 随机间隔测试发送节点 - STM32F103C8T6
  * @note           功能说明（简单测试固件）：
  *                 1. 开机初始化 LoRa 后进入主循环，每次随机等待 1~20 秒
  *                 2. 等待期间每秒打印一次倒计时
  *                 3. 到点发送一帧 LoRa（沿用以前定时帧格式）并打印发送内容
  *                 4. 只发不收，无 GPS / RTC 休眠 / 对时 / 指令解析
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

#define TX_INTERVAL_MIN_SEC 1      /* 随机间隔下限（秒） */
#define TX_INTERVAL_MAX_SEC 20     /* 随机间隔上限（秒） */
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;    /* LoRa PAN3029 */
UART_HandleTypeDef huart1;  /* 调试打印 */
UART_HandleTypeDef huart3;  /* 仅保留定义：stm32f1xx_it.c 的 USART3 中断引用 */

/* USER CODE BEGIN PV */
static uint32_t txCount = 0;   /* 发送计数，随帧发出便于核对丢帧 */
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

/**
  * @brief  产生下一次发送的随机间隔（1~20 秒）
  * @note   种子用开机 tick（每次上电时间不同，随机性够测试用）
  */
static uint32_t randIntervalSec(void)
{
  return (uint32_t)(rand() % (TX_INTERVAL_MAX_SEC - TX_INTERVAL_MIN_SEC + 1))
         + TX_INTERVAL_MIN_SEC;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU 配置 --------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* 初始化外设：GPIO(SPI 引脚/CSN/RST/IRQ/GPS 电源保持断电) + SPI1 + printf 串口 */
  MX_GPIO_Init();
  MX_NVIC_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  srand((unsigned int)HAL_GetTick() | 1u);   /* 随机种子 */

  loraInitFirstTime();

  printf("===== LoRa jamming test TX =====\r\n");
  printf("interval: %d~%d s random, TX only\r\n",
         (int)TX_INTERVAL_MIN_SEC, (int)TX_INTERVAL_MAX_SEC);
  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */
    uint32_t waitSec = randIntervalSec();
    uint32_t i;

    printf("\r\n[Schedule] next TX in %lu s\r\n", (unsigned long)waitSec);

    /* 每秒打印一次倒计时 */
    for (i = waitSec; i > 0; i--) {
      printf("[Countdown] %lu s\r\n", (unsigned long)i);
      HAL_Delay(1000);
    }

    /* 到点发送：干扰测试帧，内容随意，仅带一个递增序号便于日志核对 */
    txCount++;
    {
      char tx_msg[TX_LEN];

      snprintf(tx_msg, sizeof(tx_msg), "TEST|%lu|", (unsigned long)txCount);
      loraTx(tx_msg);
      printf("TX OK -> \"%s\"\r\n", tx_msg);
    }
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

  /* PB15 = GPS 电源：保持高电平 = 断电（本固件不用 GPS） */
  GPIO_InitStruct.Pin   = GPS_PWR_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_PWR_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);

  /* GPS 复位引脚 (PA3) - 平时高电平(不复位) */
  GPIO_InitStruct.Pin   = GPS_RST_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_RST_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET);

  /* GNSS 唤醒脚 (PB1) - 低 = Standby（GPS 断电下的安全态） */
  GPIO_InitStruct.Pin   = GNSS_WAKE_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GNSS_WAKE_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET);

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
