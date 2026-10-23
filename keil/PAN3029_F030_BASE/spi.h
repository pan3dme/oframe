#ifndef  _SPI_H_
#define  _SPI_H_
#include "stm32f0xx.h"

void SPI2_Int(void);
uint8_t SpiInputOutput_Data(SPI_TypeDef* SPIx,uint8_t InputData);


#endif


