#ifndef CENTEN_UART_H
#define CENTEN_UART_H
#include "main.h"
#include "centen_support.h"
extern CentenRxRing centenDtuRing;
extern volatile uint32_t centenDtuLastRxTick;
extern volatile uint8_t centenDtuRxRecover;
extern volatile uint32_t centenDtuErrorEvents;
void centen_dtu_receive_start(UART_HandleTypeDef *uart);
#endif
