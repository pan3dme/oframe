/*******************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file radio.h
 * @brief
 *
 * @history - V0.8, 2024-4
*******************************************************************************/
#ifndef __RADIO_H_
#define __RADIO_H_
#include "pan3029_port.h"

//#define JAP_915
//#define FCC_915
//#define ETSI_433
#define ETSI_868

#if defined(JAP_915)
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (915000000)
#define DEFAULT_SF             SF_7
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_48

#elif defined(FCC_915)
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (915000000)
#define DEFAULT_SF             SF_7
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_48

#elif defined(ETSI_433)
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (433000000)
#define DEFAULT_SF             SF_9
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_45

#elif defined(ETSI_868)
#define DEFAULT_PWR            22
#define DEFAULT_FREQ           (868000000)
#define DEFAULT_SF             SF_9
#define DEFAULT_BW             BW_125K
#define DEFAULT_CR             CODE_RATE_45

#else
/*Default parameter configuration*/
#define DEFAULT_PWR                     22
#define DEFAULT_FREQ                    (470000000)
#define DEFAULT_SF                      SF_5
#define DEFAULT_BW                      BW_500K
#define DEFAULT_CR                      CODE_RATE_45
#endif


#endif
