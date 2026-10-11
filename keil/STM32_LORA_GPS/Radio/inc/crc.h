/**
  ******************************************************************************
  * @file    crc.h
  * @brief   CRC16 utility for LoRa packet integrity
  ******************************************************************************
  */
#ifndef __CRC_H
#define __CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint16_t crc16(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __CRC_H */
