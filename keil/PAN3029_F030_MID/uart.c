#include "uart.h"
#include "dtu.h"
#include "soft_uart.h"

/* 行首状态：1=下一字符是新行第一个字符（用于自动加 {usb} 前缀） */
static int s_line_start = 1;

/* 底层发送单字符：USART1(PA9) 硬件主输出；开发阶段同时在 PA0 软串口
 * 复刻一份相同字节流（波形/波特率一致），供第二处监测 */
static void uart1_putc(uint8_t ch) {
    USART_SendData(USART1, ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
#if !DTU_PROD_MODE
    soft_uart_putc(ch); /* PA0 镜像（复刻 USART1 TX） */
#endif
}

/* 调试打印与 DTU 协议共用 USART1(PA9/PA10)：
 * 开发阶段(DTU_PROD_MODE=0)：打印每行加 {usb} 前缀，与 [dtu] 协议流区分
 * 正式装机(DTU_PROD_MODE=1)：丢弃打印，避免污染协议流 */
int fputc(int ch, FILE *f) {
    (void)f;
#if DTU_PROD_MODE
    return ch;
#else
    if (s_line_start && ch != '\r' && ch != '\n') {
        const char *tag = "{usb} ";
        while (*tag)
            uart1_putc((uint8_t)*tag++);
        s_line_start = 0;
    }
    if (ch == '\n')
        s_line_start = 1;
    uart1_putc((uint8_t)ch);
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
		USART_InitStruct1.USART_BaudRate=9600;  /* 与 PA0 镜像口统一 9600 */
		USART_InitStruct1.USART_StopBits=USART_StopBits_1;
		USART_InitStruct1.USART_Parity=USART_Parity_No;
		USART_InitStruct1.USART_WordLength=USART_WordLength_8b;
		USART_InitStruct1.USART_Mode= USART_Mode_Rx | USART_Mode_Tx;
		USART_InitStruct1.USART_HardwareFlowControl=USART_HardwareFlowControl_None;  
		USART_Init(USART1,&USART_InitStruct1);
	
	/* USART1 = DTU 专用口：始终开启接收中断 + NVIC，接收 DTU 下发数据 */
	USART_ITConfig(USART1,USART_IT_RXNE, ENABLE);
	{
		NVIC_InitTypeDef NVIC_InitStruct;
		NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
		NVIC_InitStruct.NVIC_IRQChannelPriority = 0x01;
		NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
		NVIC_Init(&NVIC_InitStruct);
	}


	/* Enable the 8xUSARTs */
	USART_Cmd(USART1,ENABLE); 
}

void Uart1_Print(uint8_t* pData,uint16_t len)
{
  uint8_t i;
  
  for(i=0;i<len;i++)
  { 
    uart1_putc(*pData); /* USART1 主输出 + PA0 镜像 */
    pData++;
  }
}

void Uart1_SendBytes(const uint8_t* pData,uint16_t len)
{
  uint16_t i;

  for(i=0;i<len;i++)
  {
    uart1_putc(pData[i]); /* USART1 主输出 + PA0 镜像 */
  }
  /* 原始发送以换行结尾时，同步 fputc 的行首状态 */
  if (len > 0 && pData[len-1] == '\n')
    s_line_start = 1;
}

