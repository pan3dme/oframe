#include "low_power.h"

/* ONLY_SEND board: leave BOOT1, SWD, HSE and LSE pins untouched.
 * Quectel L76K Reference Design V1.0, sheet 3: RESET_N and WAKEUP
 * have module-side pull-ups; the MCU only pulls low or releases them. */
#define UNUSED_A (GPIO_PIN_4 | GPIO_PIN_8 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15)
#define UNUSED_B (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | \
                  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14)
/* PA2 IRQ and PA6 MISO have no input function while the radio sleeps. */
#define STOP_CRL_MASK ((0xFu << 8) | (0xFu << 24))
/* Disconnect debug UART signals only during the full STOP interval. */
#define STOP_CRH_MASK ((0xFu << 4) | (0xFu << 8))

static uint32_t savedCrl, savedCrh, savedImr2, savedEmr2;

void LowPower_GpsPinsOff(void)
{
  GPIO_InitTypeDef pin = {0};
  pin.Mode = GPIO_MODE_ANALOG;
  pin.Pull = GPIO_NOPULL;
  pin.Pin = GPS_RST_Pin;
  HAL_GPIO_Init(GPS_RST_GPIO_Port, &pin);
  pin.Pin = GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init(GPIOB, &pin);
}

void LowPower_GpsPinsOn(void)
{
  GPIO_InitTypeDef pin = {0};
  /* L76K RESET_N is internally pulled up: release it, never source VDD. */
  HAL_GPIO_WritePin(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET);
  pin.Pin = GPS_RST_Pin;
  pin.Mode = GPIO_MODE_OUTPUT_OD;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_RST_GPIO_Port, &pin);
  /* USART3 is receive-only. Never drive the unpowered module's RXD. */
  pin.Pin = GPIO_PIN_10;
  pin.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOB, &pin);
  pin.Pin = GPIO_PIN_11;
  pin.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(GPIOB, &pin);
}

void LowPower_InitUnusedPins(void)
{
  GPIO_InitTypeDef pin = {0};
  pin.Mode = GPIO_MODE_ANALOG;
  pin.Pull = GPIO_NOPULL;
  pin.Pin = UNUSED_A;
  HAL_GPIO_Init(GPIOA, &pin);
  pin.Pin = UNUSED_B;
  HAL_GPIO_Init(GPIOB, &pin);
  pin.Pin = GPIO_PIN_13;
  HAL_GPIO_Init(GPIOC, &pin);
  LowPower_GpsPinsOff();
}

void LowPower_PrepareStop(void)
{
  /* Called once, with radio/GPS NVIC masked and SysTick stopped. Keep the
   * pins analog throughout the 3-second watchdog segments, not just WFE.
   * Change CR fields directly so EXTI routing/edge selection stays intact. */
  savedCrl = GPIOA->CRL & STOP_CRL_MASK;
  savedCrh = GPIOA->CRH & STOP_CRH_MASK;
  savedImr2 = EXTI->IMR & GPIO_PIN_2;
  savedEmr2 = EXTI->EMR & GPIO_PIN_2;
  CLEAR_BIT(EXTI->IMR, GPIO_PIN_2);
  CLEAR_BIT(EXTI->EMR, GPIO_PIN_2);
  CLEAR_BIT(GPIOA->CRL, STOP_CRL_MASK);
  CLEAR_BIT(GPIOA->CRH, STOP_CRH_MASK);
  /* CS, radio RESET, GPS power and WAKEUP retain their control levels.
   * ODR is unchanged, including the IRQ pull direction used on restore. */
}

void LowPower_AfterStop(void)
{
  MODIFY_REG(GPIOA->CRL, STOP_CRL_MASK, savedCrl);
  MODIFY_REG(GPIOA->CRH, STOP_CRH_MASK, savedCrh);
  /* The caller clears stale EXTI/NVIC pending bits before unmasking NVIC. */
  MODIFY_REG(EXTI->IMR, GPIO_PIN_2, savedImr2);
  MODIFY_REG(EXTI->EMR, GPIO_PIN_2, savedEmr2);
}
