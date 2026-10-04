/**
  ******************************************************************************
  * @file    pan3029_port.c
  * @brief   PAN3029 port layer for STM32F103 HAL
  *          Software SPI (bit-bang) + GPIO control
  ******************************************************************************
  */
#include "pan3029_port.h"
#include "pan3029_rf.h"

/* ---- Software SPI (CPOL=0, CPHA=0, MSB first) ---- */

static void spi_cs_set_high(void)
{
    HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
}

static void spi_cs_set_low(void)
{
    HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_RESET);
}

static uint8_t spi_readwritebyte(uint8_t tx_data)
{
    uint8_t i, rx = 0;
    for (i = 0; i < 8; i++)
    {
        /* MOSI: setup before rising edge */
        if (tx_data & 0x80)
            HAL_GPIO_WritePin(LORA_MOSI_GPIO_Port, LORA_MOSI_Pin, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(LORA_MOSI_GPIO_Port, LORA_MOSI_Pin, GPIO_PIN_RESET);
        tx_data <<= 1;

        /* SCK high - sample MISO */
        HAL_GPIO_WritePin(LORA_SCK_GPIO_Port, LORA_SCK_Pin, GPIO_PIN_SET);

        rx <<= 1;
        if (HAL_GPIO_ReadPin(LORA_MISO_GPIO_Port, LORA_MISO_Pin) == GPIO_PIN_SET)
            rx |= 0x01;

        /* SCK low */
        HAL_GPIO_WritePin(LORA_SCK_GPIO_Port, LORA_SCK_Pin, GPIO_PIN_RESET);
    }
    return rx;
}

/* ---- Delay functions ---- */

static void rf_delay_ms(uint32_t time)
{
    HAL_Delay(time);
}

void rf_delay_us(uint32_t time)
{
    /* Simple microsecond delay using busy loop
     * At 72MHz, approximately 18 cycles per us */
    uint32_t count = time * 18;
    while (count--)
    {
        __NOP();
    }
}

/* ---- Antenna / TCXO control (via PAN3029 internal GPIO) ---- */

static void rf_antenna_init(void)
{
    rf_set_gpio_output(MODULE_GPIO_RX);
    rf_set_gpio_output(MODULE_GPIO_TX);
    rf_set_gpio_state(MODULE_GPIO_RX, 0);
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
}

static void rf_tcxo_init(void)
{
    rf_set_gpio_output(MODULE_GPIO_TCXO);
    rf_set_gpio_state(MODULE_GPIO_TCXO, 1);
}

static void rf_tcxo_close(void)
{
    rf_set_gpio_output(MODULE_GPIO_TCXO);
    rf_set_gpio_state(MODULE_GPIO_TCXO, 0);
}

static void rf_antenna_rx(void)
{
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
    rf_set_gpio_state(MODULE_GPIO_RX, 1);
}

static void rf_antenna_tx(void)
{
    rf_set_gpio_state(MODULE_GPIO_RX, 0);
    rf_set_gpio_state(MODULE_GPIO_TX, 1);
}

static void rf_antenna_close(void)
{
    rf_set_gpio_state(MODULE_GPIO_TX, 0);
    rf_set_gpio_state(MODULE_GPIO_RX, 0);
}

/* ---- Public port object ---- */

rf_port_t rf_port = {
    .antenna_init  = rf_antenna_init,
    .tcxo_init     = rf_tcxo_init,
    .set_tx        = rf_antenna_tx,
    .set_rx        = rf_antenna_rx,
    .antenna_close = rf_antenna_close,
    .tcxo_close    = rf_tcxo_close,
    .spi_readwrite = spi_readwritebyte,
    .spi_cs_high   = spi_cs_set_high,
    .spi_cs_low    = spi_cs_set_low,
    .delayms       = rf_delay_ms,
    .delayus       = rf_delay_us,
};
