/**
  ******************************************************************************
  * @file    radio.h
  * @brief   Radio default parameter configuration
  ******************************************************************************
  */
#ifndef __RADIO_H_
#define __RADIO_H_

#include "pan3029_port.h"
#include <math.h>

/* Region selection - 433 MHz for China/Europe */
#define ETSI_433

#if defined(ETSI_433)
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (915000000)
#define DEFAULT_SF             SF_10
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_45
#else
/* Default parameter configuration */
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (433000000)
#define DEFAULT_SF             SF_7
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_48
#endif

#endif /* __RADIO_H_ */
