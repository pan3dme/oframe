/**********************************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All
 * rights reserved.
 *
 * @file main.c
 * @brief PAN3029/PAN3060 LoRa 单工通信演示程序（Simplex）
 *        - Master 模式：每隔 5 秒发送一帧数据，不开启接收，关闭 CRC
 *        - Slave  模式：持续接收数据，打印 RSSI/SNR/载荷，并与上次接收做比对
 *
 * @hardware  MCU: STM32F030  |  射频芯片: PAN3029 / PAN3060
 *            开发板: PAN3029_PAN3060_DK V2.3
 *
 * @history - V0.8, 2024-4
 ********************************************************************************************/

#include "delay.h"
#include "stm32f0xx.h"
#include "user.h"

#include "time.h"

/* ========================== 演示工程基本信息 ========================== */
#define EXAMPLE_NAME "(Simplex)PAN3029_PAN3060_DK"  /* 工程名称 */
#define DEMO_VER "V2.3"                             /* 固件版本 */
#define EXAMPLE_DATE "2025/08/30"                   /* 编译日期 */

/**
 * @brief  串口打印启动信息（工程名、版本号、日期、公司）
 */
void printf_logo(void) {
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* 工程名称        : %s\r\n", EXAMPLE_NAME);
  printf("* 固件版本        : %s\r\n", DEMO_VER);
  printf("* 编译日期        : %s\r\n", EXAMPLE_DATE);
  printf("* www.silicontra.com 矽特诺微电子有限公司\r\n");
  printf("*************************************************************\r\n");
}

/* ========================== 主从模式选择 ==============================
 * 方式一：底板按键切换
 *   S2 (PB6) → 切换为 TX 模式
 *   S3 (PB7) → 切换为 RX 模式
 * 方式二：拨码开关/跳线（Header 3 P6）
 *   PA2 高电平 = Master（发射端），低电平 = Slave（接收端）
 * ==================================================================== */
static uint8_t current_mode = 0; /* 0=RX, 1=TX */

/* 读取按键（低电平有效），返回 1 表示按下 */
#define KEY1_PRESSED  (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0)
#define KEY2_PRESSED  (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0)

/* ========================== 收发缓冲区定义 ========================== */
#define TX_LEN 32   /* 发送缓冲区长度（字节） */
#define RX_LEN 64   /* 接收缓冲区长度（字节） */

uint8_t tx_test_buf[TX_LEN] = "I LOVE U";             /* 待发送的测试数据 */
uint8_t rx_test_buf[RX_LEN] = {0};                  /* 接收数据缓冲区（保留） */
uint8_t prev_rx_buf[RX_LEN] = {0};                  /* 上一次接收的数据（用于比对） */
uint8_t prev_rx_size = 0;                           /* 上一次接收数据的长度 */

uint16_t crc_value;          /* CRC 校验值（保留） */
uint8_t Rssi_dBm;            /* 接收信号强度指示（dBm） */
uint8_t Snr_value;           /* 信噪比 SNR */
extern struct RxDoneMsg RxDoneParams; /* 接收完成时的消息结构体（由射频驱动提供） */

/* =====================================================================
 * @brief  配置系统时钟为 48 MHz
 *         HSI 8MHz / 2 = 4MHz → PLL x12 = 48MHz
 * ===================================================================== */
void SysClock_48() {
  RCC_PLLCmd(DISABLE);
  RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12); /* PLL: HSI/2 x 12 = 48MHz */
  RCC_PLLCmd(ENABLE);
  while (!RCC_GetFlagStatus(RCC_FLAG_PLLRDY))  /* 等待 PLL 锁定 */
    ;
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);   /* 切换系统时钟到 PLL 输出 */
}

/* =====================================================================
 * @brief  配置 SysTick 定时器，产生 1ms 周期中断
 * ===================================================================== */
void Tick_Configration() {
  if (SysTick_Config(SystemCoreClock / 1000)) {
    /* 配置失败，死循环 */
    while (1)
      ;
  }
}

/* =====================================================================
 * @brief  开启各外设时钟
 *         - GPIOA/B/C/F  → AHB 总线
 *         - SPI2, PWR    → APB1 总线
 *         - USART1, SYSCFG → APB2 总线
 * ===================================================================== */
void RCC_Configuration() {
  /* 使能 GPIO 端口时钟 */
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB |
                            RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOF,
                        ENABLE);

  /* 使能 SPI2 和电源管理时钟 */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2 | RCC_APB1Periph_PWR, ENABLE);

  /* 使能 USART1 和系统配置时钟 */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_SYSCFG, ENABLE);
}

/* =====================================================================
 * @brief  配置外部中断 EXTI Line1（PA1）
 *         用于射频芯片 DIO1 引脚的上升沿中断，触发收发完成事件
 * ===================================================================== */
void NVIC_Config() {
  NVIC_InitTypeDef NVIC_InitStructure;
  EXTI_InitTypeDef EXTI_InitStructure;

  /* 清除 EXTI Line1 挂起标志 */
  EXTI_ClearITPendingBit(EXTI_Line1);

  /* 配置 EXTI Line1：上升沿触发中断 */
  EXTI_InitStructure.EXTI_Line = EXTI_Line1;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  /* 将 PA1 映射到 EXTI Line1 */
  SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource1);

  /* 配置 NVIC：使能 EXTI0_1 中断，优先级 0（最高） */
  NVIC_InitStructure.NVIC_IRQChannel = EXTI0_1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

/* =====================================================================
 * @brief  硬件初始化总入口
 *         依次完成：系统时钟 → SysTick → 外设时钟 → GPIO → SPI → UART → NVIC
 * ===================================================================== */
void HW_Int()
{
  SysClock_48();       /* 系统时钟配置为 48MHz */
  Tick_Configration(); /* SysTick 1ms 中断 */
  RCC_Configuration(); /* 开启外设时钟 */
  GPIO_int();          /* GPIO 引脚初始化 */
  SPI2_Int();          /* SPI2 初始化（连接射频芯片） */
  Uart1_Int();         /* USART1 初始化（串口调试） */
  NVIC_Config();       /* 外部中断配置（射频 DIO1） */
}

/* =====================================================================
 * @brief  LED 闪烁一次（亮 50ms → 灭 50ms）
 * ===================================================================== */
void LedToggle(void) {
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET); /* LED 点亮 */
  HAL_Delay_nMs(50);
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);   /* LED 熄灭 */
  HAL_Delay_nMs(50);
}

uint32_t tx_time = 1000; /* 发送间隔参数（ms） */

/* =====================================================================
 * @brief  Master（主机/发射端）流程
 *         每隔 5 秒发送一帧数据，发送完成后等待，不开启接收
 * ===================================================================== */
void OnMaster(void) {
  static uint16_t tx_count = 0; /* 发送计数器 */
  char tx_msg[TX_LEN];

  tx_count++;
  snprintf(tx_msg, sizeof(tx_msg), "I LOVE U %u", tx_count);

  /* 发送数据 */
  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != OK) {
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); /* 发送失败，LED 熄灭 */
  }

  /* 等待发送完成标志 */
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE); /* 清除发送完成标志 */

  LedToggle();          /* LED 闪烁，表示发送成功 */
  printf("tx ok -> \"%s\"\r\n", tx_msg); /* 打印发送内容 */

  /* 等待 5 秒后进入下一次发送（基于 SysTick 硬件定时器，准确） */
  HAL_Delay_nMs(5000);
}

/* =====================================================================
 * @brief  Slave（从机/接收端）流程
 *         1. 检查是否收到完整数据包（RXDONE）
 *            - 读取 RSSI / SNR / 载荷长度 / 载荷内容
 *            - 与上一次接收数据比对，标记 [SAME] 或 [DIFF]
 *            - 串口打印接收信息
 *         2. 检查是否 CRC 错误（RXERR）
 *         3. 检查是否接收超时（RXTIMEOUT）
 * ===================================================================== */
void OnSlave(void) {
  /* ---- 接收完成：成功收到一帧数据 ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN];   /* 本次接收数据本地副本 */
    uint8_t local_size;          /* 本次数据长度本地副本 */
    uint8_t local_rssi;          /* RSSI 本地副本 */
    uint8_t local_snr;           /* SNR 本地副本 */
    uint8_t is_same = 0;         /* 与上次数据是否相同的标志 */

    /* 关中断，快速拷贝接收数据，避免被中断覆盖 */
    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN; /* 防止越界 */
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq(); /* 恢复中断 */

    /* 与上一次接收的数据逐字节比对 */
    if (local_size == prev_rx_size && prev_rx_size > 0) {
      is_same = 1;
      for (uint8_t i = 0; i < local_size; i++) {
        if (local_buf[i] != prev_rx_buf[i]) {
          is_same = 0; /* 发现不同，标记并退出比对 */
          break;
        }
      }
    }

    /* 串口打印接收信息 */
    printf("Rssi: %d  ", local_rssi - 256); /* RSSI 需减去 256 得到实际 dBm 值 */
    printf("Snr: %d   ", local_snr);
    printf("Len: %d  ", local_size);
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++) {
      if (local_buf[i] == 0) break; /* 遇到字符串结束符则停止 */
      printf("%c", local_buf[i]);
    }
    if (is_same)
      printf("  [SAME]"); /* 与上次相同 A*/
    else
      printf("  [DIFF]"); /* 与上次不同 B */
    printf("\r\n");

    /* 保存本次数据作为下次比对的基准 */
    prev_rx_size = local_size;
    for (uint8_t i = 0; i < local_size; i++)
      prev_rx_buf[i] = local_buf[i];

    LedToggle();                      /* LED 闪烁，表示收到数据 */
    rf_enter_single_timeout_rx(15000); /* 继续进入接收模式，超时 15 秒 */
  }

  /* ---- CRC 校验错误 ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    printf("crc error\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000); /* 错误后较短超时重新接收 */
  }

  /* ---- 接收超时（无数据到达） ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    printf("rx time out\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000); /* 超时后较短超时重新接收 */
  }
}

/* =====================================================================
 * @brief  主函数
 *         1. 初始化硬件和射频芯片
 *         2. 根据 RF_MODE 引脚电平确定初始角色
 *         3. 主循环中支持按键切换模式：S2→TX, S3→RX
 * ===================================================================== */
int main(void) {
  uint32_t ret = 0;

  HW_Int();       /* 硬件外设初始化 */
  Delay_Ms(1);    /* 短暂延时，等待外设稳定 */
  printf_logo();  /* 打印工程信息 */

  /* 初始化射频芯片 */
  ret = rf_init();
  if (ret != RF_OK) {
    printf("RF init fail\r\n");  /* 射频初始化失败，死循环 */
    while (1)
      ;
  }
  printf("RF init ok\r\n");
  rf_set_default_para(); /* 设置默认射频参数（频率、功率、扩频因子等） */
  rf_set_crc(false);     /* 关闭 CRC 校验 */

  /* 根据 PA2 引脚电平确定初始模式 */
  if (GPIO_ReadInputDataBit(RF_MODE_PORT, RF_MODE_PIN)) {
    current_mode = 1;
    printf("TX mode: transmit only.\r\n");
  } else {
    current_mode = 0;
    printf("RX mode: receive only.\r\n");
    rf_enter_single_timeout_rx(5000);
  }

  /* 主循环：支持按键切换模式 */
  while (1) {
    /* ---- 按键扫描（带消抖） ---- */
    if (KEY1_PRESSED) {
      Delay_Ms(20); /* 消抖 */
      if (KEY1_PRESSED && current_mode != 1) {
        current_mode = 1;
        printf("\r\n[KEY1] Switch to TX mode.\r\n");
        rf_set_mode(RF_MODE_STB1); /* 切回待机，退出接收 */
      }
      while (KEY1_PRESSED); /* 等待松手 */
      Delay_Ms(20);
    }
    if (KEY2_PRESSED) {
      Delay_Ms(20); /* 消抖 */
      if (KEY2_PRESSED && current_mode != 0) {
        current_mode = 0;
        printf("\r\n[KEY2] Switch to RX mode.\r\n");
        rf_set_mode(RF_MODE_STB1); /* 切回待机，退出发送 */
        rf_enter_single_timeout_rx(5000);
      }
      while (KEY2_PRESSED); /* 等待松手 */
      Delay_Ms(20);
    }

    /* ---- 根据当前模式执行对应流程 ---- */
    if (current_mode == 1) {
      OnMaster(); /* TX：每 5 秒发送一帧 */
    } else {
      OnSlave();  /* RX：持续接收 */
    }
  }
}
