/**********************************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All
 * rights reserved.
 *
 * @file main.c
 * @brief
 *
 * @history - V0.8, 2024-4
 ********************************************************************************************/
#include "delay.h"
#include "stm32f0xx.h"
#include "user.h"


#include "time.h"

/* 项目示例名称和版本信息 */
#define EXAMPLE_NAME "(Simplex)PAN3029_PAN3060_DK"
#define DEMO_VER "V2.3"
#define EXAMPLE_DATE "2025/08/30"

void printf_logo(void) {
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* Project Name  : %s\r\n", EXAMPLE_NAME);
  printf("* Demo Version  : %s\r\n", DEMO_VER);
  printf("* Date          : %s\r\n", EXAMPLE_DATE);
  printf("* www.silicontra.com\r\n");
  printf("*************************************************************\r\n");
}

#define EnableMaster GPIO_ReadInputDataBit(RF_MODE_PORT, RF_MODE_PIN) // 模式选择

#define TX_LEN 32
#define RX_LEN 64
// uint8_t tx_test_buf[TX_LEN] = {0, 1, 2, 3, 4, 5, 7, 7, 7, 9};
uint8_t tx_test_buf[TX_LEN] = "i love u stm32 dog"; // 任意 10 字符
uint8_t rx_test_buf[RX_LEN] = {0};
uint8_t prev_rx_buf[RX_LEN] = {0};
uint8_t prev_rx_size = 0;

uint16_t crc_value;
uint8_t Rssi_dBm; // 信号强度指示
uint8_t Snr_value;
extern struct RxDoneMsg RxDoneParams;

// 中继相关变量
volatile uint8_t send_response_flag = 0;  // 发送响应标志
volatile uint32_t send_response_time = 0; // 发送响应时间戳
uint8_t response_buf[TX_LEN] = "9|v5-0|fuck|99"; // 响应数据
      // dataStr = String(MSG_TYPE_COM) + "|" + deviceId + "|" + cmd + "|" + value;
void SysClock_48() {
  RCC_PLLCmd(DISABLE);
  RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12); // 48M
  RCC_PLLCmd(ENABLE);
  while (!RCC_GetFlagStatus(RCC_FLAG_PLLRDY))
    ;
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

void Tick_Configration() {
  /* Setup SysTick Timer for 1ms interrupts ( not too often to save power ) */
  if (SysTick_Config(SystemCoreClock / 1000)) {
    /* Capture error */
    while (1)
      ;
  }
}

void RCC_Configuration() {
  /* Enable GPIO clock */
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB |
                            RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOF,
                        ENABLE);

  /* Enable peripheral Clock */
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

  /* Enable and set EXTI0 Interrupt */
  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void HW_Int() // MCU外围资源初始化
{

  SysClock_48();
  Tick_Configration();
  RCC_Configuration();
  GPIO_int();
  SPI2_Int();
  Uart1_Int();
  NVIC_Config();
}

void LedToggle(void) {
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET); // LED闪烁
  HAL_Delay_nMs(50);
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
  HAL_Delay_nMs(50);
}

uint32_t tx_time = 1000;
/*
 * Manages the master operation
 */
void OnMaster(void) {
  // tx_test_buf[0]++;
  // tx first
  if (rf_single_tx_data(tx_test_buf, TX_LEN, &tx_time) != OK) {
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); // LED亮
  }
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  LedToggle(); // LED闪烁
  printf("tx ok\r\n");
  rf_enter_single_timeout_rx(15000);
}

/*
 * 检查是否为对时信息 (类型2)
 * 格式: 2|xxx|xxx|xxx|xxx
 */
uint8_t check_is_time_sync(uint8_t *buf, uint8_t len) {
    if (len > 0 && buf[0] == '2') {
        // 检查第二个字符是否为 '|'
        if (len > 1 && buf[1] == '|') {
            return 1; // 是对时信息
        }
    }
    return 0;
}

/*
 * Manages the slave operation
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

    /* 检查是否为对时信息 */
    if (check_is_time_sync(local_buf, local_size)) {
        printf("[收到对时信息] 将在2秒后发送响应\r\n");
        send_response_flag = 1;
        send_response_time = HAL_GetTick() + 1000; // 1秒后发送
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
  HW_Int(); // MCU初始化
  Delay_Ms(1);

  printf("\r\n===== 中继开始 =====\r\n");
  printf_logo();

  ret = rf_init();

  if (ret != RF_OK) {
    printf("RF init fail\r\n");
    while (1)
      ;
  }
  printf("RF init ok\r\n");
  rf_set_default_para(); // 设置射频频率参数 (会打印LORA配置信息)

  printf("===== 进入监听状态 =====\r\n");
  rf_enter_single_timeout_rx(5000); // 进入单接收状态

  while (1) {
    OnSlave(); // 监听

    // 检查是否需要发送响应
    if (send_response_flag) {
      if (HAL_GetTick() >= send_response_time) {
        send_response_flag = 0;
        printf("[发送响应] %s\r\n", response_buf);
        uint32_t tx_time = 1000;
        if (rf_single_tx_data(response_buf, strlen((char*)response_buf), &tx_time) != OK) {
          printf("发送失败\r\n");
        }
        // 等待发送完成
        while (rf_get_transmit_flag() == RADIO_FLAG_IDLE);
        rf_set_transmit_flag(RADIO_FLAG_IDLE);
        printf("发送完成\r\n");
        // 重新进入接收模式
        rf_enter_single_timeout_rx(15000);
      }
    }
  }
}
