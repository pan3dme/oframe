#include "uart.h"
#include "dtu.h"

int fputc(int ch, FILE *f) {
#if (SERIAL_MODE == SERIAL_MODE_DTU)
    /* DTU 模式：串口接 DTU 模块，丢弃调试打印，避免污染协议流 */
    (void)f;
    return ch;
#else
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
#endif
}

void Uart1_Int(void)
{
	
   USART_InitTypeDef  USART_InitStruct1;
   GPIO_InitTypeDef  GPIO_InitStruct;
	
	 /****************************************
		 UART1_TX
		****************************************/
		GPIO_InitStruct.GPIO_OType=GPIO_OType_PP;
		GPIO_InitStruct.GPIO_PuPd=GPIO_PuPd_NOPULL;
		GPIO_InitStruct.GPIO_Speed=GPIO_Speed_Level_2;
		GPIO_InitStruct.GPIO_Mode=GPIO_Mode_AF;
		GPIO_InitStruct.GPIO_Pin=UART1_TX_PIN;
		GPIO_Init(UART1_TX_PORT, &GPIO_InitStruct);
		GPIO_PinAFConfig(UART1_TX_PORT,GPIO_PinSource9,GPIO_AF_1);
		
	 
		/****************************************
		 UART1_RX
		****************************************/
		GPIO_InitStruct.GPIO_OType=GPIO_OType_PP;
		GPIO_InitStruct.GPIO_PuPd=GPIO_PuPd_UP;
		GPIO_InitStruct.GPIO_Speed=GPIO_Speed_Level_2;
		GPIO_InitStruct.GPIO_Mode=GPIO_Mode_AF;
		GPIO_InitStruct.GPIO_Pin=UART1_RX_PIN;
		GPIO_Init(UART1_RX_PORT, &GPIO_InitStruct);
		GPIO_PinAFConfig(UART1_TX_PORT,GPIO_PinSource10,GPIO_AF_1);
	
	

		USART_DeInit(USART1);
		USART_InitStruct1.USART_BaudRate=115200;
		USART_InitStruct1.USART_StopBits=USART_StopBits_1;
		USART_InitStruct1.USART_Parity=USART_Parity_No;
		USART_InitStruct1.USART_WordLength=USART_WordLength_8b;
		USART_InitStruct1.USART_Mode= USART_Mode_Rx | USART_Mode_Tx;
		USART_InitStruct1.USART_HardwareFlowControl=USART_HardwareFlowControl_None;  
		USART_Init(USART1,&USART_InitStruct1);
	
	/* Enable 8xUSARTs Receive interrupts */
#if (SERIAL_MODE == SERIAL_MODE_DTU)
	/* DTU 模式：开启接收中断 + NVIC，接收 DTU 下发数据 */
	USART_ITConfig(USART1,USART_IT_RXNE, ENABLE);
	{
		NVIC_InitTypeDef NVIC_InitStruct;
		NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
		NVIC_InitStruct.NVIC_IRQChannelPriority = 0x01;
		NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStruct);
	}
#else
  /* DISABLE 8xUSARTs Receive interrupts */
	USART_ITConfig(USART1,USART_IT_RXNE, DISABLE);
#endif


	/* Enable the 8xUSARTs */
	USART_Cmd(USART1,ENABLE); 
}

void Uart1_Print(uint8_t* pData,uint16_t len)
{
  uint8_t i;
  
  for(i=0;i<len;i++)
  { 
    USART_SendData(USART1, *pData);
    while(USART_GetFlagStatus(USART1,USART_FLAG_TXE) == RESET);
    pData++;
  }
}

void Uart1_SendBytes(const uint8_t* pData,uint16_t len)
{
  uint16_t i;

  for(i=0;i<len;i++)
  {
    USART_SendData(USART1, pData[i]);
    while(USART_GetFlagStatus(USART1,USART_FLAG_TXE) == RESET);
  }
}

