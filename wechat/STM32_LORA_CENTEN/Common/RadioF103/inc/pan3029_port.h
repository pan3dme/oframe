/**
  ******************************************************************************
  * @file    pan3029_port.h
  * @brief   PAN3029 hardware abstraction layer
  *          Adapted from Panchip reference driver for STM32F103 HAL
  ******************************************************************************
  */
#ifndef __PAN3029_PORT_H_
#define __PAN3029_PORT_H_

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- LoRa SPI GPIO pins (software bit-bang) --- */
#define LORA_SCK_Pin      GPIO_PIN_5
#define LORA_SCK_GPIO_Port GPIOA
#define LORA_MISO_Pin     GPIO_PIN_6
#define LORA_MISO_GPIO_Port GPIOA
#define LORA_MOSI_Pin     GPIO_PIN_7
#define LORA_MOSI_GPIO_Port GPIOA
#define LORA_CSN_Pin      GPIO_PIN_0
#define LORA_CSN_GPIO_Port GPIOB
#define LORA_RST_Pin      GPIO_PIN_1
#define LORA_RST_GPIO_Port GPIOA

/* PAN3029 internal GPIO pins (controlled via SPI registers) */
#define MODULE_GPIO_TX    0
#define MODULE_GPIO_RX    10
#define MODULE_GPIO_TCXO  3

typedef struct {
    void (*antenna_init)(void);
    void (*tcxo_init)(void);
    void (*set_tx)(void);
    void (*set_rx)(void);
    void (*antenna_close)(void);
    void (*tcxo_close)(void);
    uint8_t (*spi_readwrite)(uint8_t tx_data);
    void (*spi_cs_high)(void);
    void (*spi_cs_low)(void);
    void (*delayms)(uint32_t time);
    void (*delayus)(uint32_t time);
} rf_port_t;

extern rf_port_t rf_port;

/* Port function implementations */
uint8_t spi_readwritebyte(uint8_t tx_data);
void    spi_cs_set_high(void);
void    spi_cs_set_low(void);
void    rf_delay_ms(uint32_t time);
void    rf_delay_us(uint32_t time);
void    rf_antenna_init(void);
void    rf_tcxo_init(void);
void    rf_tcxo_close(void);
void    rf_antenna_rx(void);
void    rf_antenna_tx(void);
void    rf_antenna_close(void);

/* PAN3029 internal GPIO control (defined in pan3029_rf.c) */

#endif /* __PAN3029_PORT_H_ */
