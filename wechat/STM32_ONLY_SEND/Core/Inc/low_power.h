#ifndef LOW_POWER_H
#define LOW_POWER_H

#include "main.h"

/* GPIO clocks must already be enabled; call at boot while GPS is off. */
void LowPower_InitUnusedPins(void);
void LowPower_GpsPinsOn(void);
void LowPower_GpsPinsOff(void);
/* Pair around the entire STOP interval with radio/GPS NVIC masked. */
void LowPower_PrepareStop(void);
void LowPower_AfterStop(void);

#endif
