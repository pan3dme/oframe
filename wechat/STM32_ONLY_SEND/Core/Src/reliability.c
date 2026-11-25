#include "reliability.h"

/* F103C8 backup registers, not Flash: survive MCU reset, no erase wear.
 * DR1 magic, DR2 explicit reason, DR3 phase, DR4 failure streak, DR5 LSI hint.
 * rtc_wake_test.c is a separate, excluded test application. */
#define SAFETY_MAGIC 0x5732u
#define IWDG_RELOAD  1561u
static uint32_t feedTick;
static uint32_t phaseTick;
static uint32_t phaseLimit;
static uint32_t rtcValue, rtcTick;
static uint8_t rtcSeen;
static uint8_t streak;
static uint8_t forceLsi;
static uint8_t lastStage;
static uint8_t currentStage;
static const char *bootCause = "POR";

static void backup_enable(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  SET_BIT(PWR->CR, PWR_CR_DBP);
}

void Safety_Init(void)
{
  uint32_t guard = 100000u;
  uint32_t flags = RCC->CSR;
  uint16_t reason = RESET_NONE;

  /* Start first. There is deliberately no tick dependency in this path. */
  IWDG->KR = 0xCCCCu;
  IWDG->KR = 0x5555u;
  IWDG->PR = 6u; /* /256, use the documented encoding */
  IWDG->RLR = IWDG_RELOAD;
  while (IWDG->SR && --guard) { }
  if (IWDG->SR) {
    NVIC_SystemReset();
  }
  IWDG->KR = 0xAAAAu;

  backup_enable();
  if (!(flags & RCC_CSR_PORRSTF) && BKP->DR1 == SAFETY_MAGIC) {
    reason = (uint16_t)BKP->DR2;
    lastStage = (uint8_t)BKP->DR3;
    streak = (BKP->DR4 <= 15u) ? (uint8_t)BKP->DR4 : 0u;
    forceLsi = (BKP->DR5 == 1u);
  }
  if (flags & RCC_CSR_IWDGRSTF) {
    bootCause = "IWDG";
    if (lastStage == STAGE_STOP || lastStage == STAGE_RTC)
      forceLsi = 1u;
  } else if (flags & RCC_CSR_SFTRSTF) {
    static const char *const names[] = {
      "SW", "INIT", "RTC", "TX", "RX", "SPI", "RF", "LINK", "FLASH",
      "ADC", "NMI", "HARD", "MEM", "BUS", "USAGE", "REMOTE", "LIMIT"
    };
    bootCause = (reason < sizeof(names) / sizeof(names[0])) ? names[reason] : "SW";
  } else if (!(flags & RCC_CSR_PORRSTF)) {
    bootCause = "PIN";
  }
  /* Count real faults only: an IWDG bite (no chance to record a reason),
   * or a Safety_Reset with a recorded reason.  A bare software reset with
   * reason == RESET_NONE is a debugger/download artifact (SysWResreq) and
   * must not accumulate the fault streak. */
  if ((flags & (RCC_CSR_IWDGRSTF | RCC_CSR_SFTRSTF)) && streak < 15u) {
    if ((flags & RCC_CSR_IWDGRSTF) ||
        (reason != RESET_NONE && reason != RESET_REMOTE))
      ++streak;
  }
  if (reason == RESET_RTC)
    forceLsi = 1u;
  RCC->CSR |= RCC_CSR_RMVF;
  currentStage = STAGE_BOOT;
  BKP->DR1 = SAFETY_MAGIC;
  BKP->DR2 = RESET_NONE;
  BKP->DR3 = currentStage;
  BKP->DR4 = streak;
  BKP->DR5 = forceLsi;
  phaseLimit = 15000u;
}

void Safety_Begin(SafetyStage stage, uint32_t maxMs)
{
  currentStage = (uint8_t)stage;
  BKP->DR3 = currentStage;
  phaseTick = HAL_GetTick();
  phaseLimit = maxMs;
}

void Safety_Feed(void)
{
  uint32_t now = HAL_GetTick();
  if (phaseLimit && (uint32_t)(now - phaseTick) >= phaseLimit)
    Safety_Reset(RESET_DEADLINE);
  /* A busy wait with a frozen SysTick can feed at most once, never forever. */
  if (now != feedTick) {
    IWDG->KR = 0xAAAAu;
    feedTick = now;
  }
}

/* Stage transitions must never hide a stopped scheduling clock. */
void Safety_CheckRtc(uint32_t rtc)
{
  uint32_t now = HAL_GetTick();
  if (!rtcSeen || rtc != rtcValue) {
    rtcSeen = 1u;
    rtcValue = rtc;
    rtcTick = now;
  } else if ((uint32_t)(now - rtcTick) >= 3000u) {
    Safety_Reset(RESET_RTC);
  }
}

void Safety_PrepareStop(void)
{
  currentStage = STAGE_STOP;
  BKP->DR3 = currentStage;
  /* Exactly once per RTC segment; no feeding on spurious wakes. */
  IWDG->KR = 0xAAAAu;
}

void Safety_AfterStop(void)
{
  IWDG->KR = 0xAAAAu;
  feedTick = HAL_GetTick();
  Safety_Begin(STAGE_MAIN, 15000u);
}

void Safety_Reset(SafetyReason reason)
{
  __disable_irq();
  backup_enable();
  BKP->DR2 = (uint16_t)reason;
  if (reason == RESET_RTC)
    BKP->DR5 = 1u;
  __DSB();
  NVIC_SystemReset();
  for (;;) { } /* IWDG remains the final fallback. */
}

void Safety_ResetBackupDomain(void)
{
  backup_enable();
  RCC->BDCR |= RCC_BDCR_BDRST;
  RCC->BDCR &= ~RCC_BDCR_BDRST;
  BKP->DR1 = SAFETY_MAGIC;
  BKP->DR2 = RESET_NONE;
  BKP->DR3 = currentStage;
  BKP->DR4 = streak;
  BKP->DR5 = forceLsi;
}

void Safety_MarkLinkHealthy(void)
{
  streak = 0u;
  BKP->DR4 = 0u;
}

uint8_t Safety_UseLsi(void) { return forceLsi; }
uint8_t Safety_HadFault(void) { return streak != 0u; }
uint8_t Safety_LastStage(void) { return lastStage; }
const char *Safety_BootCause(void) { return bootCause; }
uint32_t Safety_BackoffSeconds(void)
{
  /* Only repeated faults back off; remote reboot and healthy boots don't. */
  if (streak < 3u) return 0u;
  if (streak >= 7u) return 600u;
  return 30u << (streak - 3u); /* 30, 60, 120, 240 seconds */
}
