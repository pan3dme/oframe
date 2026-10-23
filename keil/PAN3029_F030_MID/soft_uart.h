#ifndef _SOFT_UART_H_
#define _SOFT_UART_H_

#include "stm32f0xx.h"
#include <stdint.h>

/* =====================================================================
 * 镜像监测口：软件模拟 UART（仅 TX），引脚 = PA0
 * 作用：把 USART1 TX(PA9) 发出的字节流在 PA0 上复刻一份，
 *       波特率/帧格式完全相同（9600 8N1），接收端接线与参数不用改
 * 弃用历史：PA11/PA12 实测乱码；PA0 待验证
 * 排查手段：main.c 的 pa0_contention_probe() 用内部弱上拉/弱下拉读 PA0 电平，
 *           判断该网络是否被外部电路（如 PAN3029 模块天线/TCXO 控制输出）钉住
 * 波特率固定 9600, 8N1（与主口 USART1 统一）
 * ===================================================================== */
#define SOFT_UART_TX_PORT   GPIOA
#define SOFT_UART_TX_PIN    GPIO_Pin_0
#define SOFT_UART_BAUD      9600

void soft_uart_init(void);            /* GPIO 初始化（空闲高电平） */
void soft_uart_putc(uint8_t ch);      /* 发送 1 字节（阻塞，期间关中断） */
void soft_uart_send(const uint8_t *p, uint16_t len);

#endif
