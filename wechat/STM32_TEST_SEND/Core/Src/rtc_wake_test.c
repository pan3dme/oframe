/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : rtc_wake_test.c
  * @brief          : RTC 唤醒测试 v23 —— 直接 WFE 事件唤醒（精简+全程进度打印）
  ******************************************************************************
  * v21/v22 取证：STOP 期间 RTC 全程走时（walked=216s），事件链醒着全通。
  * v23 砍掉所有旁支，直接测 WFE 事件唤醒；每步打印防卡死失联。
  ******************************************************************************
  */
/* USER CODE END Header */
#include "stm32f1xx_hal.h"
#include <stdio.h>

UART_HandleTypeDef huart3;   /* it.c 链接需要 */
SPI_HandleTypeDef hspi1;     /* pan3029_port 链接需要 */

volatile uint32_t rtcIrqCount = 0;

#define TEST_PERIOD_S  10
#define BKP_MAGIC      0xBEEFU

/* ---------------- USART1: PA9(TX) 115200，寄存器直配 ---------------- */
static void uart1_init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  GPIOA->CRH = (GPIOA->CRH & ~0xF0U) | 0xB0U;

  USART1->BRR = 69;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

int fputc(int ch, FILE *f)
{
  (void)f;
  while (!(USART1->SR & USART_SR_TXE))
    ;
  USART1->DR = (uint8_t)ch;
  return ch;
}

static void uart_flush(void)
{
  while (!(USART1->SR & USART_SR_TC))
    ;
}

static uint32_t mcu_sec(void)
{
  return HAL_GetTick() / 1000;
}

/* ---------------- RTC ---------------- */
static void rtc_wait_rtoff(void)
{
  uint32_t guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RTOFF) && --guard)
    ;
}

static uint32_t rtc_read_counter(void)
{
  /* 唤醒后 RTC 接口需 2 个 RTC 时钟重新同步：
   * 软件清 RSF 再等硬件重新置位，否则读到 STOP 前的影子值 */
  uint32_t guard = 200000;

  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  (void)RTC->CRL;
  guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard)
    ;
  return ((uint32_t)RTC->CNTH << 16) | (uint32_t)RTC->CNTL;
}

static void rtc_clear_flags(void)
{
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~(RTC_CRL_ALRF | RTC_CRL_SECF);
  (void)RTC->CRL;
}

static void rtc_set_alarm(uint32_t alarmSec)
{
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->ALRH = (uint16_t)(alarmSec >> 16);
  RTC->ALRL = (uint16_t)(alarmSec & 0xFFFFU);
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  (void)RTC->CRL;
  rtc_wait_rtoff();
}

static void rtc_init(void)
{
  uint32_t guard;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  SET_BIT(PWR->CR, PWR_CR_DBP);
  printf("[init] pwr/bkp ok\r\n");
  uart_flush();

  /* 备份域保留（不 BDRST，保 BKP 取证）；确认时钟源在跑 */
  {
    uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;

    if (sel == RCC_BDCR_RTCSEL_LSI) {
      RCC->CSR |= RCC_CSR_LSION;
      guard = 200000;
      while (!(RCC->CSR & RCC_CSR_LSIRDY) && --guard)
        ;
    } else if (sel == RCC_BDCR_RTCSEL_LSE) {
      RCC->BDCR |= RCC_BDCR_LSEON;
      guard = 60000000;
      while (!(RCC->BDCR & RCC_BDCR_LSERDY) && --guard)
        ;
    }
    printf("[init] clock ready (sel=%lu)\r\n", (unsigned long)(sel >> 8));
    uart_flush();
  }

  guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard)
    ;
  printf("[init] rsf ok\r\n");
  uart_flush();

  /* EXTI17：事件模式（EMR）+ 中断模式都开；NVIC 关（纯事件唤醒） */
  EXTI->RTSR |= EXTI_RTSR_TR17;
  EXTI->EMR |= EXTI_EMR_MR17;
  EXTI->IMR |= EXTI_IMR_MR17;
  EXTI->PR = EXTI_PR_PR17;
  printf("[init] exti ok (EMR=1 IMR=1)\r\n");
  uart_flush();
}

/* ---------------- main ---------------- */
int main(void)
{
  uint32_t lastBase = 0;
  uint8_t  haveBase = 0;

  HAL_Init();
  SystemCoreClock = 8000000U;
  SysTick_Config(SystemCoreClock / 1000);

  uart1_init();

  printf("\r\n===== RTC WAKE TEST v23 (WFE direct) =====\r\n");
  uart_flush();

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  if (BKP->DR4 == BKP_MAGIC) {
    lastBase = ((uint32_t)BKP->DR1 << 16) | (uint32_t)BKP->DR2;
    haveBase = 1;
  }

  rtc_init();

  {
    uint32_t now = rtc_read_counter();

    printf("[t=%lu]cnt now=%lu\r\n", (unsigned long)mcu_sec(),
           (unsigned long)now);
    if (haveBase)
      printf("[t=%lu]>>> FORENSIC: STOP base=%lu walked=%lu s\r\n",
             (unsigned long)mcu_sec(), (unsigned long)lastBase,
             (unsigned long)(now - lastBase));
    uart_flush();
  }

  /* ---------- WFE STOP 循环（ALRIE=0，纯事件路径） ---------- */
  for (;;) {
    static uint32_t round = 0;
    uint32_t t0, t1;

    round++;
    rtc_clear_flags();
    EXTI->PR = EXTI_PR_PR17;

    t0 = rtc_read_counter();
    rtc_set_alarm(t0 + TEST_PERIOD_S);

    BKP->DR1 = (uint16_t)(t0 >> 16);
    BKP->DR2 = (uint16_t)(t0 & 0xFFFFU);
    BKP->DR4 = BKP_MAGIC;

    printf("[t=%lu][#%lu] cnt=%lu STOP/WFE (%us)...\r\n",
           (unsigned long)mcu_sec(), (unsigned long)round,
           (unsigned long)t0, (unsigned)TEST_PERIOD_S);
    uart_flush();

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFE);

    BKP->DR4 = 0;
    SysTick_Config(8000);
    t1 = rtc_read_counter();
    rtc_clear_flags();
    EXTI->PR = EXTI_PR_PR17;

    printf("[t=%lu][WAKE] #%lu elapsed=%lu s (cnt=%lu)\r\n",
           (unsigned long)mcu_sec(), (unsigned long)round,
           (unsigned long)(t1 - t0), (unsigned long)t1);
    uart_flush();
  }
}
