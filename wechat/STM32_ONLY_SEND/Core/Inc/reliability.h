#ifndef RELIABILITY_H
#define RELIABILITY_H

#include "stm32f1xx_hal.h"

typedef enum {
  STAGE_BOOT = 1, STAGE_CLOCK, STAGE_RTC, STAGE_MAIN,
  STAGE_RADIO, STAGE_TX, STAGE_RX, STAGE_GPS,
  STAGE_TRACK_WAIT, STAGE_STOP, STAGE_FLASH, STAGE_ADC
} SafetyStage;

typedef enum {
  RESET_NONE = 0, RESET_INIT, RESET_RTC, RESET_TX, RESET_RX,
  RESET_SPI, RESET_RADIO, RESET_LINK, RESET_FLASH, RESET_ADC,
  RESET_NMI, RESET_HARDFAULT, RESET_MEMFAULT, RESET_BUSFAULT,
  RESET_USAGEFAULT, RESET_REMOTE, RESET_DEADLINE
} SafetyReason;

/* Called before HAL/clock/peripheral initialization, using reset HSI. */
void Safety_Init(void);
void Safety_Begin(SafetyStage stage, uint32_t maxMs);
void Safety_Feed(void);
/* Independent of stage re-arming; HAL tick advances only while awake. */
void Safety_CheckRtc(uint32_t rtc);
void Safety_PrepareStop(void);
void Safety_AfterStop(void);
void Safety_Reset(SafetyReason reason);
void Safety_ResetBackupDomain(void);
void Safety_MarkLinkHealthy(void);
uint8_t Safety_UseLsi(void);
uint8_t Safety_HadFault(void);
uint32_t Safety_BackoffSeconds(void);
const char *Safety_BootCause(void);
uint8_t Safety_LastStage(void);

#endif
