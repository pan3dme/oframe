/**
  ******************************************************************************
  * @file    crc.c
  * @brief   CRC16-CCITT implementation
  ******************************************************************************
  */
#include "crc.h"

/**
  * @brief  Calculate CRC16-CCITT (polynomial 0x1021)
  * @param  data: pointer to data buffer
  * @param  len:  data length in bytes
  * @retval CRC16 value
  */
uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;

    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}
