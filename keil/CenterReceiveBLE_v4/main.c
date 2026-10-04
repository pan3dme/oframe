/**********************************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All
 * rights reserved.
 *
 * @file main.c
 * @brief CenterReceiveBLE_v4 - 3-key control: S1=Reset, S2=TX(3s), S3=RX
 *
 * @history - V0.9, 2026-10
 ********************************************************************************************/
#include "delay.h"
#include "stm32f0xx.h"
#include "user.h"

#include "time.h"

/* Project info */
#define EXAMPLE_NAME "(Simplex)PAN3029_PAN3060_DK"
#define DEMO_VER "V2.4"
#define EXAMPLE_DATE "2026/10/04"

void printf_logo(void) {
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* Project Name  : %s\r\n", EXAMPLE_NAME);
  printf("* Demo Version  : %s\r\n", DEMO_VER);
  printf("* Date          : %s\r\n", EXAMPLE_DATE);
  printf("* www.silicontra.com\r\n");
  printf("*************************************************************\r\n");
}

#define TX_LEN 32
#define RX_LEN 64
uint8_t tx_test_buf[TX_LEN] = "i love u stm32 dog";
uint8_t rx_test_buf[RX_LEN] = {0};
uint8_t prev_rx_buf[RX_LEN] = {0};
uint8_t prev_rx_size = 0;

uint16_t crc_value;
uint8_t Rssi_dBm;
uint8_t Snr_value;
extern struct RxDoneMsg RxDoneParams;

/* Mode definition */
#define MODE_RX  0
#define MODE_TX  1
volatile uint8_t current_mode = MODE_RX;

/* Button debounce */
#define KEY_DEBOUNCE_MS  50

/* Relay response variables */
volatile uint8_t send_response_flag = 0;
volatile uint32_t send_response_time = 0;
uint8_t response_buf[TX_LEN] = "9|v5-0|fuck|99";

void SysClock_48() {
  RCC_PLLCmd(DISABLE);
  RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12);
  RCC_PLLCmd(ENABLE);
  while (!RCC_GetFlagStatus(RCC_FLAG_PLLRDY))
    ;
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

void Tick_Configration() {
  if (SysTick_Config(SystemCoreClock / 1000)) {
    while (1)
      ;
  }
}

void RCC_Configuration() {
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB |
                            RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOF,
                        ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2 | RCC_APB1Periph_PWR, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_SYSCFG, ENABLE);
}

void NVIC_Config() {
  NVIC_InitTypeDef NVIC_InitStructure;
  EXTI_InitTypeDef EXTI_InitStructure;

  EXTI_ClearITPendingBit(EXTI_Line1);
  EXTI_InitStructure.EXTI_Line = EXTI_Line1;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource1);

  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void HW_Int() {
  SysClock_48();
  Tick_Configration();
  RCC_Configuration();
  GPIO_int();
  SPI2_Int();
  Uart1_Int();
  NVIC_Config();
}

void LedToggle(void) {
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET);
  HAL_Delay_nMs(50);
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
  HAL_Delay_nMs(50);
}

/*
 * Button scan with debounce
 * Returns: 1=KEY1(PA0/S1), 2=KEY2(PB6/S2), 3=KEY3(PB7/S3), 0=no key
 */
uint8_t Key_Scan(void) {
  static uint32_t key_last_time = 0;
  uint32_t now = HAL_GetTick();

  /* Debounce interval check */
  if (now - key_last_time < KEY_DEBOUNCE_MS)
    return 0;

  /* KEY1: PA0 (active low) */
  if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == Bit_RESET) {
    key_last_time = now;
    return 1;
  }
  /* KEY2: PB6 (active low) */
  if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == Bit_RESET) {
    key_last_time = now;
    return 2;
  }
  /* KEY3: PB7 (active low) */
  if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == Bit_RESET) {
    key_last_time = now;
    return 3;
  }

  return 0;
}

/*
 * TX mode: send data every 3 seconds
 */
void OnMaster(void) {
  static uint32_t last_tx_time = 0;
  uint32_t now = HAL_GetTick();

  if (now - last_tx_time >= 3000) {
    last_tx_time = now;
    uint32_t tx_time = 1000;
    if (rf_single_tx_data(tx_test_buf, TX_LEN, &tx_time) != OK) {
      GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
    }
    while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
      ;
    rf_set_transmit_flag(RADIO_FLAG_IDLE);
    LedToggle();
    printf("TX [3s] ok\r\n");
  }
}

/*
 * Check if received data is time sync info (type 2)
 * Format: 2|xxx|xxx|xxx|xxx
 */
uint8_t check_is_time_sync(uint8_t *buf, uint8_t len) {
    if (len > 0 && buf[0] == '2') {
        if (len > 1 && buf[1] == '|') {
            return 1;
        }
    }
    return 0;
}

/*
 * RX mode: receive and process data
 */
void OnSlave(void) {
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN];
    uint8_t local_size;
    uint8_t local_rssi;
    uint8_t local_snr;
    uint8_t is_same = 0;

    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    /* compare with previous */
    if (local_size == prev_rx_size && prev_rx_size > 0) {
      is_same = 1;
      for (uint8_t i = 0; i < local_size; i++) {
        if (local_buf[i] != prev_rx_buf[i]) {
          is_same = 0;
          break;
        }
      }
    }

    printf("Rssi: %d  ", local_rssi - 256);
    printf("Snr: %d   ", local_snr);
    printf("Len: %d  ", local_size);
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++) {
      if (local_buf[i] == 0) break;
      printf("%c", local_buf[i]);
    }
    if (is_same)
      printf("  [SAME]");
    else
      printf("  [DIFF]");
    printf("\r\n");

    /* Check time sync info */
    if (check_is_time_sync(local_buf, local_size)) {
        printf("[time sync] send response in 1s\r\n");
        send_response_flag = 1;
        send_response_time = HAL_GetTick() + 1000;
    }

    /* save current as previous */
    prev_rx_size = local_size;
    for (uint8_t i = 0; i < local_size; i++)
      prev_rx_buf[i] = local_buf[i];

    LedToggle();
    rf_enter_single_timeout_rx(15000);
  }
  if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    printf("crc error\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);
  }
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    printf("rx time out\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);
  }
}

int main(void) {
  uint32_t ret = 0;
  uint8_t key;

  HW_Int();
  Delay_Ms(1);

  printf("\r\n===== System Start =====\r\n");
  printf_logo();

  ret = rf_init();
  if (ret != RF_OK) {
    printf("RF init fail\r\n");
    while (1)
      ;
  }
  printf("RF init ok\r\n");
  rf_set_default_para();

  printf("===== Enter RX Mode (S1=Reset, S2=TX, S3=RX) =====\r\n");
  rf_enter_single_timeout_rx(5000);

  while (1) {
    /* Button scan */
    key = Key_Scan();
    if (key == 1) {
      /* S1: Reset */
      printf("\r\n[S1] System Reset...\r\n");
      HAL_Delay_nMs(500);
      NVIC_SystemReset();
    }
    else if (key == 2) {
      /* S2: Switch to TX mode */
      if (current_mode != MODE_TX) {
        current_mode = MODE_TX;
        printf("\r\n[S2] Switch to TX Mode (send every 3s)\r\n");
        LedToggle();
      }
    }
    else if (key == 3) {
      /* S3: Switch to RX mode */
      if (current_mode != MODE_RX) {
        current_mode = MODE_RX;
        printf("\r\n[S3] Switch to RX Mode\r\n");
        LedToggle();
        rf_enter_single_timeout_rx(5000);
      }
    }

    /* Mode handling */
    if (current_mode == MODE_TX) {
      OnMaster();
    } else {
      OnSlave();

      /* Check relay response */
      if (send_response_flag) {
        if (HAL_GetTick() >= send_response_time) {
          send_response_flag = 0;
          printf("[send response] %s\r\n", response_buf);
          uint32_t tx_time = 1000;
          if (rf_single_tx_data(response_buf, strlen((char*)response_buf), &tx_time) != OK) {
            printf("send fail\r\n");
          }
          while (rf_get_transmit_flag() == RADIO_FLAG_IDLE);
          rf_set_transmit_flag(RADIO_FLAG_IDLE);
          printf("send done\r\n");
          rf_enter_single_timeout_rx(15000);
        }
      }
    }
  }
}