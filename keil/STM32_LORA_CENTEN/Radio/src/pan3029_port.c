/**
  ******************************************************************************
  * @file    pan3029_port.c
  * @brief   PAN3029 port layer for STM32F103 HAL
  *          Hardware SPI1 + GPIO control
  ******************************************************************************
  */
#include "pan3029_port.h"
#include "pan3029_rf.h"

/* 外部引用 main.c 中定义的 SPI1 句柄 */
extern SPI_HandleTypeDef hspi1;

/* ---- Hardware SPI1 (CPOL=0, CPHA=0, MSB first, 4.5MHz) ---- */

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
    uint8_t rx_data = 0;
    HAL_SPI_TransmitReceive(&hspi1, &tx_data, &rx_data, 1, 100);
    return rx_data;
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
