/**********************************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file main.c
 * @brief
 *
 * @history - V0.8, 2024-4
********************************************************************************************/
#include "stm32f0xx.h"
#include "delay.h"
#include "user.h"

#include "time.h"

/* 定义例程名和例程发布日期 */
#define EXAMPLE_NAME	"(Simplex)PAN3029_PAN3060_DK"
#define DEMO_VER	"V2.3"
#define EXAMPLE_DATE	"2025/08/30"

void printf_logo(void)
{
	  printf("\n\r");
    printf("*************************************************************\r\n");
    printf("* 例程名称        : %s\r\n", EXAMPLE_NAME);	
    printf("* 例程版本        : %s\r\n", DEMO_VER);		
    printf("* 发布日期        : %s\r\n", EXAMPLE_DATE);
    printf("* www.silicontra.com 深圳硅传科技有限公司\r\n");
    printf("*************************************************************\r\n");
}

#define  EnableMaster        GPIO_ReadInputDataBit(RF_MODE_PORT, RF_MODE_PIN)//主从选择脚

#define TX_LEN 10
#define RX_LEN 64
uint8_t tx_test_buf[TX_LEN] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
uint8_t rx_test_buf[RX_LEN] = {0};
uint16_t crc_value;
uint8_t Rssi_dBm; //信号强度指示
uint8_t Snr_value;
extern struct RxDoneMsg RxDoneParams;

void SysClock_48()
{
  RCC_PLLCmd(DISABLE);
  RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12); // 48M
  RCC_PLLCmd(ENABLE);
  while (!RCC_GetFlagStatus(RCC_FLAG_PLLRDY))
    ;
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

void Tick_Configration()
{
  /* Setup SysTick Timer for 1ms interrupts ( not too often to save power ) */
  if (SysTick_Config(SystemCoreClock / 1000))
  {
    /* Capture error */
    while (1)
      ;
  }
}

void RCC_Configuration()
{
  /* Enable GPIO clock */
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOF, ENABLE);

  /* Enable peripheral Clock */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2 | RCC_APB1Periph_PWR, ENABLE);

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_SYSCFG, ENABLE);
}
void NVIC_Config()
{
  NVIC_InitTypeDef NVIC_InitStructure;
  EXTI_InitTypeDef EXTI_InitStructure;

  EXTI_ClearITPendingBit(EXTI_Line1);
  EXTI_InitStructure.EXTI_Line = EXTI_Line1;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource1);
	
  /* Enable and set EXTI0 Interrupt */
  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void HW_Int() // MCU外围资源初始化
{

  SysClock_48();
  Tick_Configration();
  RCC_Configuration();
  GPIO_int();
  SPI2_Int();
	Uart1_Int();
  NVIC_Config();
}

void LedToggle(void)
{
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET); // LED闪烁
  HAL_Delay_nMs(50);
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
  HAL_Delay_nMs(50);
}

uint32_t tx_time = 1000;
/*
 * Manages the master operation
 */
void OnMaster(void)
{
	  tx_test_buf[0]++;
  // tx first
  if (rf_single_tx_data(tx_test_buf, TX_LEN, &tx_time) != OK)
  {
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); // LED灭
  }
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  LedToggle(); // LED闪烁
  printf("tx ok\r\n");
  rf_enter_single_timeout_rx(15000);
      
}

/*
 * Manages the slave operation
 */
void OnSlave(void)
{
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
  {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
		

    Rssi_dBm = RxDoneParams.Rssi;
		
    Snr_value = RxDoneParams.Snr;
 	
		
			printf("Rssi: %d  ",Rssi_dBm-256);
			printf("Snr: %d   ",Snr_value);
			printf("Data: ");
			for (uint8_t i = 0; i < RxDoneParams.Size; i++)
			printf("%02x ", RxDoneParams.Payload[i]);
			printf("\r\n");  
			
		  LedToggle(); 
		
    rf_enter_single_timeout_rx(15000); //重新进入接收模式
                       // LED闪
  }
		// rxtimeout or rxerr flag
		if(rf_get_recv_flag() == RADIO_FLAG_RXERR)
		{
			printf("crc error\r\n");
			rf_set_recv_flag(RADIO_FLAG_IDLE);
			rf_enter_single_timeout_rx(5000); //重新进入接收模式
		}
	
	  if(rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
	  {
	   printf("rx time out\r\n");
     rf_set_recv_flag(RADIO_FLAG_IDLE);
     rf_enter_single_timeout_rx(5000); //重新进入接收模式
	  }
	
}

int main(void)
{
  uint32_t ret = 0;
  HW_Int(); // MCU初始化
  Delay_Ms(1);
	printf_logo();
	
  ret = rf_init();
	
  if (ret != RF_OK)
  {
	  printf("RF init fail\r\n");
    while (1)
      ;
  }	
    printf("RF init ok\r\n");	
  rf_set_default_para(); //配置射频参数
	
  if (EnableMaster == true)
  {
		 printf("RF tx test start.\r\n");
  }
  else
  {
		 printf("RF rx test start.\r\n");
     rf_enter_single_timeout_rx(5000);//进入单次接收状态
		 //rf_enter_continous_rx();//进入连续接收状态
  }

  while (1)
  {	 
   if (EnableMaster) 
    {
      OnMaster();//主机
    }
    else
    {
      OnSlave(); //从机
    }
		
  }
		
}
