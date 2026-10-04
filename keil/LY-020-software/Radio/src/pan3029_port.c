/*******************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file pan3029_port.c
 * @brief PAN3029 端口层 — STM32F1 HAL 软件 SPI 版本
 *
 * @history - V0.8, 2024-4
*******************************************************************************/
#include "pan3029_port.h"

rf_port_t rf_port=
{
    .antenna_init = rf_antenna_init,
    .tcxo_init = rf_tcxo_init,
    .set_tx = rf_antenna_tx,
    .set_rx = rf_antenna_rx,
    .antenna_close = rf_antenna_close,
    .tcxo_close = rf_tcxo_close,
    .spi_readwrite = spi_readwritebyte,
    .spi_cs_high = spi_cs_set_high,
    .spi_cs_low = spi_cs_set_low,
    .delayms = rf_delay_ms,
    .delayus = rf_delay_us,
};

/* ---------- 软件 SPI 辅助宏 ---------- */
#define SPI_SCK_HIGH()    HAL_GPIO_WritePin(RF_SCK_PORT,  RF_SCK_PIN,  GPIO_PIN_SET)
#define SPI_SCK_LOW()     HAL_GPIO_WritePin(RF_SCK_PORT,  RF_SCK_PIN,  GPIO_PIN_RESET)
#define SPI_MOSI_HIGH()   HAL_GPIO_WritePin(RF_MOSI_PORT, RF_MOSI_PIN, GPIO_PIN_SET)
#define SPI_MOSI_LOW()    HAL_GPIO_WritePin(RF_MOSI_PORT, RF_MOSI_PIN, GPIO_PIN_RESET)
#define SPI_MISO_READ()   HAL_GPIO_ReadPin(RF_MISO_PORT, RF_MISO_PIN)

/**
 * @brief  软件 SPI 读写一个字节 (CPOL=0, CPHA=0, MSB first)
 * @param  tx_data  待发送字节
 * @return 接收到的字节
 */
uint8_t spi_readwritebyte(uint8_t tx_data)
{
    uint8_t rx_data = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        /* 发送位 (MSB first) */
        if (tx_data & 0x80)
            SPI_MOSI_HIGH();
        else
            SPI_MOSI_LOW();

        SPI_SCK_HIGH();                       /* 上升沿, 从机采样 */
        rx_data = (rx_data << 1) | SPI_MISO_READ();
        SPI_SCK_LOW();                        /* 下降沿, 从机移位 */
        tx_data <<= 1;
    }
    return rx_data;
}

/**
 * @brief  CSN 拉高 ( deselect )
 */
void spi_cs_set_high(void)
{
    HAL_GPIO_WritePin(RF_CSN_PORT, RF_CSN_PIN, GPIO_PIN_SET);
}

/**
 * @brief  CSN 拉低 ( select )
 */
void spi_cs_set_low(void)
{
    HAL_GPIO_WritePin(RF_CSN_PORT, RF_CSN_PIN, GPIO_PIN_RESET);
}

/**
 * @brief  毫秒延时
 */
void rf_delay_ms(uint32_t time)
{
    HAL_Delay(time);
}

/**
 * @brief  微秒延时 (粗略, 72 MHz 下约 9 个 NOP / us)
 */
void rf_delay_us(uint32_t time)
{
    while (time--)
    {
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP();
    }
}

/* ---------- PAN3029 内部 GPIO 操作 (通过 SPI 寄存器) ---------- */

void rf_antenna_init(void)
{
    rf_set_gpio_output(MODULE_GPIO_RX);
    rf_set_gpio_output(MODULE_GPIO_TX);

    rf_set_gpio_state(MODULE_GPIO_RX, 0);
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
}

void rf_tcxo_init(void)
{
    rf_set_gpio_output(MODULE_GPIO_TCXO);
    rf_set_gpio_state(MODULE_GPIO_TCXO, 1);
}

void rf_tcxo_close(void)
{
    rf_set_gpio_output(MODULE_GPIO_TCXO);
    rf_set_gpio_state(MODULE_GPIO_TCXO, 0);
}

void rf_antenna_rx(void)
{
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
    rf_set_gpio_state(MODULE_GPIO_RX, 1);
}

void rf_antenna_tx(void)
{
    rf_set_gpio_state(MODULE_GPIO_RX, 0);
    rf_set_gpio_state(MODULE_GPIO_TX, 1);
}

void rf_antenna_close(void)
{
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
    rf_set_gpio_state(MODULE_GPIO_RX, 0);
}
