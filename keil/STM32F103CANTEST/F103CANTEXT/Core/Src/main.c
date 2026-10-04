/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */


//CAN全局变量
CAN_TxHeaderTypeDef TxHeaderCAN;
CAN_RxHeaderTypeDef RxHeaderCAN;
uint8_t RxDataCAN[8];
uint8_t TxDataCAN[8];
//uint8_t TxDataCAN[8]= {99,25,33,48,99,78,63,99};
uint8_t CAN_Rx_Flag=0;//CAN接收标志
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void can_filterconfig_and_init(void);
void filter_init(void);
void CAN_Test(void);
void CAN1_Send_Test(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

 #ifdef __GNUC__
     #define PUTCHAR_PROTOTYPE int _io_putchar(int ch)
 #else
     #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
 #endif /* __GNUC__*/
 
 /******************************************************************
     *@brief  Retargets the C library printf  function to the USART.
     *@param  None
     *@retval None
 ******************************************************************/
 PUTCHAR_PROTOTYPE
 {
     HAL_UART_Transmit(&huart1, (uint8_t *)&ch,1,0xFFFF);
     return ch;
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
	MX_CAN_Init();
  /* USER CODE BEGIN 2 */
	can_filterconfig_and_init(); //CAN配置
	

	
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
//		printf("lanyuan\r\n");
		//HAL_Delay(500);
		
		HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
	  HAL_Delay(200);
	  HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
	  HAL_Delay(200);
	  HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
	  HAL_Delay(200);
	  HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
	  HAL_Delay(200);
	  HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
	  HAL_Delay(200);
	  HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
	  HAL_Delay(200);
		
		//HAL_Delay(200);
		//CAN_Test();
	 	 //处理CAN接收数据
	 	if(CAN_Rx_Flag)
	 	{
	 	 CAN_Rx_Flag = 0;//清空CAN接收标志
	 	 printf("CAN接收数据\r\n");
	 	 for(int i = 0;i<8;i++) printf(" 0x%02x",RxDataCAN[i]);
			HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
	 	 printf("\r\n");
	 	}
		
		
		
		
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
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
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 4;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_5TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_3TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);

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

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */



//重定义CAN接收中断回调函数
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
	
    HAL_StatusTypeDef HAL_Status;
    if(hcan->Instance == CAN1)
    {
        HAL_Status = HAL_CAN_GetRxMessage(hcan,CAN_RX_FIFO0,&RxHeaderCAN,RxDataCAN);
        if(HAL_Status == HAL_OK)
        {
						//处理接收数据
          CAN_Rx_Flag = 1;
					//HAL_CAN_ActivateNotification(hcan,CAN_IT_RX_FIFO0_MSG_PENDING);//再次使能中断接收
        }
    }
		
		
		
		
}

void CAN_Test(void)
{
	 //发送数据CAN
	      TxHeaderCAN.ExtId = 0x00;
	      TxHeaderCAN.DLC = 8;
	      TxHeaderCAN.IDE = CAN_ID_STD;
	      TxHeaderCAN.RTR = CAN_RTR_DATA;
	      TxHeaderCAN.StdId = 0x00;
	      TxHeaderCAN.TransmitGlobalTime = ENABLE;

	      uint32_t TxMailBox;
	      HAL_StatusTypeDef HAL_Status;

    printf("\r\n------------------CAN通信测试------------------\r\n");

//   for(int i = 0; i < 8; ++i)
//		{
//			TxDataCAN[i] = i;
//		}
		
    printf("CAN发送数据：\r\n");
    for (int i = 0; i < 8; ++i) 
		{
			printf(" 0x%02x",TxDataCAN[i]);
		}
		printf("\r\n");


    HAL_CAN_AddTxMessage(&hcan,&TxHeaderCAN,TxDataCAN,&TxMailBox);
}



void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == K1_Pin)
    {  // KEY_PIN 需替换为你的引脚（如 GPIO_PIN_0）
        // 简单消抖：延时检测电平是否稳定
        //HAL_Delay(10);  // 注意：中断内慎用延时，实际项目建议用标记位+主循环处理
        if (HAL_GPIO_ReadPin(K1_GPIO_Port, K1_Pin) == GPIO_PIN_RESET)
        {
            // 执行按键操作，如翻转 LED
        	HAL_GPIO_TogglePin(LED3_GPIO_Port, LED3_Pin);
            printf("KEY1 trigger \r\n");
      
        }
    }

    if (GPIO_Pin == K2_Pin) {  // KEY_PIN 需替换为你的引脚（如 GPIO_PIN_0）
            // 简单消抖：延时检测电平是否稳定
          //  HAL_Delay(10);  // 注意：中断内慎用延时，实际项目建议用标记位+主循环处理
            if (HAL_GPIO_ReadPin(K2_GPIO_Port, K2_Pin) == GPIO_PIN_RESET)
            {
                // 执行按键操作，如翻转 LED
            	HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
                printf("KEY2 trigger\r\n");
						
            }
        }


}

void can_filterconfig_and_init(void) //CAN滤波器配置
{
	CAN_FilterTypeDef filter;
	filter.FilterIdHigh = 0x0000;
	filter.FilterIdLow = 0x0000;
	filter.FilterMaskIdHigh = 0x0000;
	filter.FilterMaskIdLow = 0x0000;
	//不过滤
	filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
	filter.FilterActivation = ENABLE;
	filter.FilterMode = CAN_FILTERMODE_IDMASK;
	filter.FilterScale = CAN_FILTERSCALE_32BIT;
	filter.FilterBank = 0;
	//filter.SlaveStartFilterBank =14;
	
	if(HAL_CAN_ConfigFilter(&hcan,&filter) != HAL_OK)
	{
		Error_Handler();
		printf("can配置失败\r\n");
	}	
	else
		printf("can配置成功\r\n");
	
	if(HAL_CAN_ActivateNotification(&hcan,CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
	{
		Error_Handler();
		printf("开启挂起中断允许失败\r\n");
	}	
	else
		printf("开启挂起中断允许成功\r\n");
	if(HAL_CAN_Start(&hcan) != HAL_OK)
	{
		Error_Handler();
		printf("can开启失败\r\n");
	}	
	else
		printf("can开启成功\r\n");
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
