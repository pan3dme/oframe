#include "centen_uart.h"

CentenRxRing centenDtuRing;
volatile uint32_t centenDtuLastRxTick;
volatile uint8_t centenDtuRxRecover;
volatile uint32_t centenDtuErrorEvents;
static uint8_t receiveByte;

void centen_dtu_receive_start(UART_HandleTypeDef *uart)
{
    centenDtuRxRecover = HAL_UART_Receive_IT(uart, &receiveByte, 1) != HAL_OK;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART3) return;
    centen_ring_push(&centenDtuRing, receiveByte);
    centenDtuLastRxTick = HAL_GetTick();
    /* Always re-arm, including the ring-full/discard path. */
    centen_dtu_receive_start(uart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART3) {
        centenDtuErrorEvents++;
        centenDtuRxRecover = 1;
    }
}
