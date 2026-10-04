/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa Receiver - STM32F103C8T6
  *                   Matches CenterReceiveBLE_v4 LoRa configuration
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
#include "string.h"
#include "pan3029_rf.h"
#include "pan3029_port.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LY-020 LoRa Receiver"
#define DEMO_VER     "V1.0"
#define EXAMPLE_DATE "2026/10/04"

#define RX_LEN       64

/* Work mode */
#define MODE_IDLE    0
#define MODE_RX      1
#define MODE_TX      2
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint32_t lastLedTick = 0;
static uint32_t lastLed1Tick = 0;
static uint8_t  workMode = MODE_IDLE;
static uint8_t  txMsg[] = "I LOVE YOU";
static uint32_t lastTxTick = 0;
static uint32_t txLedOnTick = 0;
static uint32_t lastKeyTick = 0;

/* LoRa receive buffers */
static uint8_t  prev_rx_buf[RX_LEN] = {0};
static uint8_t  prev_rx_size = 0;

extern struct RxDoneMsg RxDoneParams;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */
static void printf_logo(void);
static void OnSlave(void);
static void LedToggle(void);
static void enter_rx_mode(void);
static void do_tx_send(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1,(uint8_t *)&ch,1,0xFFFF);
  return ch;
}

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

static void LedToggle(void)
{
  HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
  HAL_Delay(50);
  HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
  HAL_Delay(50);
}

/**
  * @brief  LoRa slave receive handler (same logic as CenterReceiveBLE_v4)
  */
static void OnSlave(void)
{
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
  {
    uint8_t local_buf[RX_LEN];
    uint8_t local_size;
    int8_t  local_rssi;
    float   local_snr;
    uint8_t is_same = 0;

    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr  = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    /* compare with previous */
    if (local_size == prev_rx_size && prev_rx_size > 0)
    {
      is_same = 1;
      for (uint8_t i = 0; i < local_size; i++)
      {
        if (local_buf[i] != prev_rx_buf[i])
        {
          is_same = 0;
          break;
        }
      }
    }

    printf("Rssi: %d  ", local_rssi - 256);
    printf("Snr: %d   ", (int)local_snr);
    printf("Len: %d  ", local_size);
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++)
    {
      if (local_buf[i] == 0) break;
      printf("%c", local_buf[i]);
    }
    if (is_same)
      printf("  [SAME]");
    else
      printf("  [DIFF]");
    printf("\r\n");

    /* save current as previous */
    prev_rx_size = local_size;
    for (uint8_t i = 0; i < local_size; i++)
      prev_rx_buf[i] = local_buf[i];

    LedToggle();
    rf_enter_single_timeout_rx(15000);
  }

  if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
  {
    printf("crc error\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);
  }

  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
  {
    printf("rx time out\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);
  }
}

/**
  * @brief  Enter RX mode
  */
static void enter_rx_mode(void)
{
    workMode = MODE_RX;
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    printf("\r\n>> Switch to RX mode\r\n");
    rf_enter_single_timeout_rx(5000);
}

/**
  * @brief  Handle TX mode: send "I LOVE YOU" every 3s, LED 0.5s on
  */
static void do_tx_send(void)
{
    uint32_t now = HAL_GetTick();

    /* Send every 3 seconds */
    if (now - lastTxTick >= 3000)
    {
        lastTxTick = now;
        uint32_t tx_time;
        rf_single_tx_data(txMsg, sizeof(txMsg) - 1, &tx_time);
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        txLedOnTick = now;
        printf("TX: I LOVE YOU\r\n");
    }

    /* LED off after 500ms */
    if (HAL_GPIO_ReadPin(LED1_GPIO_Port, LED1_Pin) == GPIO_PIN_SET)
    {
        if (now - txLedOnTick >= 500)
        {
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        }
    }

    /* Clear TX done flag */
    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        rf_set_transmit_flag(RADIO_FLAG_IDLE);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();

  /* Initialize NVIC for LoRa IRQ (PA2 EXTI) */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */

  printf("\r\n===== LoRa Receiver Start =====\r\n");
  printf_logo();

  HAL_Delay(1);

  /* LoRa PAN3029 init */
  printf("LoRa init...\r\n");

  /* Ensure CSN high, RST high */
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);  /* Wait for module power stable */

  if (rf_init() != RF_OK)
  {
    printf("RF init fail! Check wiring.\r\n");
    while (1)
    {
      /* LED4 heartbeat - always blink */
      if (HAL_GetTick() - lastLedTick >= 1000)
      {
        lastLedTick = HAL_GetTick();
        HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
      }
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
      HAL_Delay(100);
    }
  }
  printf("RF init ok\r\n");

  /* Set default LoRa parameters (same as CenterReceiveBLE_v4 ETSI_868):
   *   Freq = 915 MHz, SF = 10, BW = 125 kHz, CR = 4/5, Power = 22
   * This also prints: FREQ= xxx  SF=x   BW=x  CR=x */
  rf_set_default_para();

  printf("===== LoRa Ready =====\r\n");
  printf("  Freq    : 915.000 MHz\r\n");
  printf("  SF      : 10\r\n");
  printf("  BW      : 125 kHz\r\n");
  printf("  CR      : 4/5\r\n");
  printf("  Power   : 22 dBm\r\n");
  printf("  CRC     : OFF\r\n");
  printf("  SyncWord: 0x12\r\n");
  printf("----------------------------\r\n");
  printf("  K1 -> RX mode (LED 500ms blink)\r\n");
  printf("  K2 -> TX mode (send 'I LOVE YOU' / 3s)\r\n");
  printf("----------------------------\r\n");

  enter_rx_mode();  /* Default: enter RX mode */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* LED4 (PA0) 1s blink - heartbeat (always) */
    if (HAL_GetTick() - lastLedTick >= 1000)
    {
      lastLedTick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
    }

    /* Mode handling */
    switch (workMode)
    {
      case MODE_RX:
        /* LED1: 500ms blink */
        if (HAL_GetTick() - lastLed1Tick >= 500)
        {
          lastLed1Tick = HAL_GetTick();
          HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        }
        OnSlave();
        break;

      case MODE_TX:
        do_tx_send();
        break;

      default:
        break;
    }

    HAL_Delay(10);
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
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED1_Pin LED2_Pin LED3_Pin */
  GPIO_InitStruct.Pin = LED1_Pin|LED2_Pin|LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : K1_Pin K2_Pin */
  GPIO_InitStruct.Pin = K1_Pin|K2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* LED4: PA0 output */
  GPIO_InitStruct.Pin  = LED4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED4_GPIO_Port, &GPIO_InitStruct);

  /* LoRa SPI GPIO: PA5-SCK, PA6-MISO, PA7-MOSI  (output for bit-bang) */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin  = LORA_MISO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LoRa CSN: PB0 output, default HIGH */
  GPIO_InitStruct.Pin  = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);

  /* LoRa RST: PA1 output, default HIGH */
  GPIO_InitStruct.Pin  = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);

  /* LoRa IRQ: PA2 input, rising edge interrupt */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
  * @brief NVIC Configuration for LoRa IRQ (PA2 -> EXTI2)
  */
static void MX_NVIC_Init(void)
{
  /* EXTI2 interrupt for LoRa IRQ (PA2) */
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/* USER CODE BEGIN 4 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* LoRa IRQ: PA2 -> call rf_irq_process */
    if (GPIO_Pin == GPIO_PIN_2)
    {
        rf_irq_process();
    }

    /* K1 (PB4) -> Enter RX mode (with debounce) */
    if (GPIO_Pin == K1_Pin)
    {
        if (HAL_GPIO_ReadPin(K1_GPIO_Port, K1_Pin) == GPIO_PIN_RESET)
        {
            if (HAL_GetTick() - lastKeyTick > 300)
            {
                lastKeyTick = HAL_GetTick();
                enter_rx_mode();
            }
        }
    }

    /* K2 (PB5) -> Enter TX mode (with debounce) */
    if (GPIO_Pin == K2_Pin)
    {
        if (HAL_GPIO_ReadPin(K2_GPIO_Port, K2_Pin) == GPIO_PIN_RESET)
        {
            if (HAL_GetTick() - lastKeyTick > 300)
            {
                lastKeyTick = HAL_GetTick();
                workMode = MODE_TX;
                lastTxTick = 0;  /* Force immediate first send */
                HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
                printf("\r\n>> Switch to TX mode\r\n");
            }
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
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
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
