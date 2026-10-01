#include "stm32f0xx.h"
#include "gpio.h"

void GPIO_int()
{
  GPIO_InitTypeDef GPIO_InitStruct;
  /****************************************
   RF_NSS
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStruct.GPIO_Pin = RF_CSN_IO;
  GPIO_Init(RF_CSN_PORT, &GPIO_InitStruct);
  GPIO_SetBits(RF_CSN_PORT,RF_CSN_IO);
  /****************************************
   M_CLK
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStruct.GPIO_Pin = RF_SCK_IO;
  GPIO_Init(RF_SCK_PORT, &GPIO_InitStruct);
  GPIO_PinAFConfig(RF_SCK_PORT, RF_SCK_AF, GPIO_AF_0);

  /****************************************
   M_MOSI
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStruct.GPIO_Pin = RF_MOSI_IO;
  GPIO_Init(RF_MOSI_PORT, &GPIO_InitStruct);
  GPIO_PinAFConfig(RF_MOSI_PORT, RF_MOSI_AF, GPIO_AF_0);

  /****************************************
   M_MISO
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStruct.GPIO_Pin = RF_MISO_IO;
  GPIO_Init(RF_MISO_PORT, &GPIO_InitStruct);
  GPIO_PinAFConfig(RF_MISO_PORT, RF_MISO_AF, GPIO_AF_0);

  /****************************************
   RF_IRQ
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStruct.GPIO_Pin = RF_IRQ_IO;
  GPIO_Init(RF_IRQ_PORT, &GPIO_InitStruct);
 
	/****************************************
	 RF_CAD
	****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStruct.GPIO_Pin = CAD_IO;
  GPIO_Init(CAD_PORT, &GPIO_InitStruct);
	
  /****************************************
   LED1
  ****************************************/
  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStruct.GPIO_Pin = LED1_PIN;
  GPIO_Init(LED1_PORT, &GPIO_InitStruct);
	
	GPIO_SetBits(LED1_PORT,LED1_PIN);
	
						 /*  RF_MODE_PIN  */
	GPIO_InitStruct.GPIO_Pin =RF_MODE_PIN; 
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_DOWN; 
	GPIO_Init(RF_MODE_PORT, &GPIO_InitStruct);
	
	
}
