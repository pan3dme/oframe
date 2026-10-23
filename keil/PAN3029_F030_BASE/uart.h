#ifndef UART_H
#define UART_H
#include "stm32f0xx.h"
#include <stdio.h>

#define UART1_TX_PIN       GPIO_Pin_9
#define UART1_TX_PORT      GPIOA

#define UART1_RX_PIN       GPIO_Pin_10
#define UART1_RX_PORT      GPIOA




void Uart1_Int(void);
void Uart1_Print(uint8_t* pData,uint16_t len);


#endif
