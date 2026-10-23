/**********************************************************************************************
 * @file soft_uart.c
 * @brief 软件模拟 UART 发送（镜像监测口 PA0，复刻 USART1 TX 的字节流）
 *        帧格式与硬件一致：9600 8N1，字节背靠背无额外间隔
 *        - 位时序用 SysTick->VAL（48MHz 向下计数）校准，与编译优化等级无关
 *        - 每字节发送期间关中断：防止 EXTI0_1 内 rf_irq_process（SPI 事务耗时数百 us）
 *          打断位时序造成乱码；9600 时单字节关中断约 1.04ms，SysTick 会挂起
 *          并在恢复中断后立即补服，不丢节拍
 *        - 仅实现 TX：调试打印为单向输出；DTU 协议收发走 USART1 硬件串口
 ********************************************************************************************/
#include "soft_uart.h"

/* 1 个位的周期数（48MHz 主频）：9600bps → 5000 周期 */
#define BIT_CYCLES   (48000000UL / SOFT_UART_BAUD)

/* =====================================================================
 * @brief  等待距 start 时刻过去 cycles 个核心周期（SysTick 向下计数，
 *         手动处理 1ms 周期回卷）
 * @return 到达时刻的 SysTick->VAL
 * ===================================================================== */
static uint32_t wait_until(uint32_t start, uint32_t cycles) {
  uint32_t period = SysTick->LOAD + 1; /* SysTick 计数周期（48000） */
  for (;;) {
    uint32_t now = SysTick->VAL;
    uint32_t elapsed = (now <= start) ? (start - now)
                                      : (start + period - now);
    if (elapsed >= cycles)
      return now;
  }
}

/* =====================================================================
 * @brief  初始化：PA0 推挽输出，空闲高电平
 *         （覆盖 gpio.c 中 CAD 的上拉输入配置；CAD 当前流程未使用）
 * ===================================================================== */
void soft_uart_init(void) {
  GPIO_InitTypeDef GPIO_InitStruct;

  GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Level_2;
  GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
  GPIO_InitStruct.GPIO_Pin   = SOFT_UART_TX_PIN;
  GPIO_Init(SOFT_UART_TX_PORT, &GPIO_InitStruct);
  GPIO_SetBits(SOFT_UART_TX_PORT, SOFT_UART_TX_PIN); /* 空闲高 */
}

/* =====================================================================
 * @brief  发送 1 字节：1 起始位(低) + 8 数据位(LSB 先) + 1 停止位(高)
 * ===================================================================== */
void soft_uart_putc(uint8_t ch) {
  uint32_t saved = __get_PRIMASK();
  uint32_t t;
  int i;

  __disable_irq();

  t = SysTick->VAL;
  GPIO_ResetBits(SOFT_UART_TX_PORT, SOFT_UART_TX_PIN); /* 起始位 */
  for (i = 0; i < 8; i++) {
    t = wait_until(t, BIT_CYCLES);
    if ((ch >> i) & 0x01)
      GPIO_SetBits(SOFT_UART_TX_PORT, SOFT_UART_TX_PIN);
    else
      GPIO_ResetBits(SOFT_UART_TX_PORT, SOFT_UART_TX_PIN);
  }
  t = wait_until(t, BIT_CYCLES);
  GPIO_SetBits(SOFT_UART_TX_PORT, SOFT_UART_TX_PIN);   /* 停止位 */
  wait_until(t, BIT_CYCLES);
  /* 不加字节间空闲槽：严格复刻 USART1 硬件波形（NANO 实测可读） */

  if (!saved)
    __enable_irq(); /* 恢复调用前的中断状态 */
}

/* =====================================================================
 * @brief  发送一段数据
 * ===================================================================== */
void soft_uart_send(const uint8_t *p, uint16_t len) {
  uint16_t i;
  for (i = 0; i < len; i++)
    soft_uart_putc(p[i]);
}
