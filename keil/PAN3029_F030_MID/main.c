/**********************************************************************************************
 * @file main.c
 * @brief PAN3029 LoRa 集中器主程序（功能同 STM32_LORA_CENTEN）
 *        - LoRa 接收终端设备上报 → 经 USART1 以 JSON 上报 DTU
 *        - USART1 接收 DTU 下发：网络对时 / 配置 / 指令缓存 / gpstm 中继
 *        - 收到上报后延时下发：缓存指令 > gpstm(upgps) > 对时消息
 *
 * @hardware  MCU: STM32F030  |  射频芯片: PAN3029 / PAN3060
 *            串口: USART1(PA9/PA10) 调试与 DTU 共用（USB 串口主输出，115200 8N1）
 *            - 开发阶段(DTU_PROD_MODE=0): 调试打印带 {usb} 前缀, 协议流带 [dtu] 前缀
 *            - 正式装机(DTU_PROD_MODE=1): 串口纯跑 DTU 协议, 无前缀无调试打印
 *
 * @history - V3.0, 2026-10  由 CENTEN 迁移, 移除旧 TX/RX 双模式逻辑
 ********************************************************************************************/

#include "delay.h"
#include "stm32f0xx.h"
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================== 演示工程基本信息 ========================== */
#define EXAMPLE_NAME "stm32集中器"  /* 工程名称 */
#define DEMO_VER "V3.0"                             /* 固件版本 */
#define EXAMPLE_DATE "2026/10/23"                   /* 编译日期 */

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

/* ========================== 时间同步变量 ========================== */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间戳（DTU 网络时间） */
static uint32_t syncedTick = 0;       /* 同步时的 HAL_GetTick() 值 */
static uint8_t  timeSynced = 0;       /* 是否已同步时间：0=未同步, 1=已同步 */

/**
 * @brief  获取当前完整时间戳
 * @return Unix 时间戳（未同步时返回开机秒数）
 */
uint32_t getCurrentTimestamp(void) {
  if (!timeSynced) {
    return HAL_GetTick() / 1000;
  }
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  return syncedTimestamp + elapsed_sec;
}

/**
 * @brief  将 DTU 网络时间写入主时间源（由 dtu.c 调用）
 * @param  ts  Unix 时间戳
 */
void app_set_synced_time(uint32_t ts) {
  syncedTimestamp = ts;
  syncedTick = HAL_GetTick();
  timeSynced = 1;
}

/* ========================== 收发缓冲区定义 ========================== */
#define RX_LEN 64              /* 接收缓冲区长度（字节） */
#define LORA_TX_DELAY_MS 1500  /* LoRa 接收后延时下发时间（ms） */

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
  Uart1_Int();         /* USART1 初始化（调试打印 + DTU 协议，PA9/PA10） */
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

/* =====================================================================
 * @brief  集中器接收处理（同 CENTEN OnSlave）
 *         1. 收到完整包：打印 RSSI/SNR/载荷 → 上报 DTU → 延时 1.5s →
 *            下发缓存指令 > gpstm 中继(upgps) > 对时消息
 *         2. CRC 标志：CRC 关闭时数据仍可读，照常打印/上报
 *         3. 接收超时：重新进入接收
 * ===================================================================== */
void OnSlave(void) {
  /* ---- 接收完成：成功收到一帧数据 ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN];   /* 本次接收数据本地副本 */
    uint8_t local_size;          /* 本次数据长度本地副本 */
    uint8_t local_rssi;          /* RSSI 本地副本 */
    uint8_t local_snr;           /* SNR 本地副本 */

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

    /* 串口打印接收信息 */
    printf("Rssi: %d  ", local_rssi - 256); /* RSSI 需减去 256 得到实际 dBm 值 */
    printf("Snr: %d   ", local_snr);
    printf("Len: %d  ", local_size);
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++) {
      if (local_buf[i] == 0) break; /* 遇到字符串结束符则停止 */
      printf("%c", local_buf[i]);
    }
    printf("\r\n");

    LedToggle(); /* LED 闪烁，表示收到数据 */

    /* ---- 集中器处理：上报 DTU → 延时 → 缓存指令 / gpstm / 对时 ---- */
    {
      int msgType;
      char loraDevId[32] = {0};
      uint8_t hasPendingCmd = 0;

      if (local_size < RX_LEN)
        local_buf[local_size] = '\0';
      msgType = atoi((char *)local_buf);
      printf("[LoRa] MsgType: %d\r\n", msgType);

      if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_TIME) {
        /* 提取 deviceId（第 2 个 '|' 分隔字段），检查缓存指令 */
        const char *p1 = strchr((char *)local_buf, '|');
        if (p1) {
          const char *p2 = p1 + 1;
          const char *p3 = strchr(p2, '|');
          int idLen = p3 ? (int)(p3 - p2) : (int)strlen(p2);
          if (idLen > 31) idLen = 31;
          memcpy(loraDevId, p2, idLen);
          loraDevId[idLen] = '\0';
          hasPendingCmd = dtu_check_pending(loraDevId);
        }

        /* 上报 DTU：TIME 消息替换时间字段为当前时间戳 */
        if (msgType == MSG_TYPE_TIME) {
          char reportBuf[RX_LEN];
          if (dtu_replace_time_segment((char *)local_buf, reportBuf,
                                       sizeof(reportBuf)) == 0)
            dtu_report_lora(reportBuf, local_rssi - 256, local_snr);
          else
            dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
        } else {
          /* MSG_TYPE_GPS: 直接上报原始数据 */
          dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
        }

        /* 延时后下发（同 CENTEN）：缓存指令 > gpstm 中继 > 对时消息
         * 无缓存指令且无 gpstm 时，无条件 LoRa 下发对时（未同步时下发开机秒数） */
        Delay_Ms(LORA_TX_DELAY_MS);
        if (hasPendingCmd) {
          dtu_send_pending(loraDevId);
        } else if (dtu_gpstm_active()) {
          dtu_send_upgps(loraDevId);
        } else {
          dtu_send_time_reply();
        }
      } else if (msgType == MSG_TYPE_CONFIG) {
        /* 配置消息：原样上报，不对时 */
        dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
      }
    }

    rf_enter_single_timeout_rx(15000); /* 继续进入接收模式，超时 15 秒 */
  }

  /* ---- CRC 标志（CRC 关闭时数据仍可读，照常打印） ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    uint8_t local_buf[RX_LEN];
    uint8_t local_size;
    uint8_t local_rssi;
    uint8_t local_snr;

    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    if (local_size > 0) {
      printf("Rssi: %d  ", local_rssi - 256);
      printf("Snr: %d   ", local_snr);
      printf("Len: %d  ", local_size);
      printf("Str: ");
      for (uint8_t i = 0; i < local_size; i++) {
        if (local_buf[i] == 0) break;
        printf("%c", local_buf[i]);
      }
      printf("  [CRC-FLAG]\r\n");
    } else {
      printf("crc error (no data)\r\n");
    }
    rf_enter_single_timeout_rx(5000); /* 重新接收 */
  }

  /* ---- 接收超时（无数据到达） ---- */
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    printf("rx time out\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000); /* 超时后较短超时重新接收 */
  }
}

/* =====================================================================
 * @brief  主函数（集中器：上电即持续接收）
 *         1. 初始化硬件和射频芯片
 *         2. 初始化 DTU 协议层（加载 Flash 配置）
 *         3. 主循环：DTU 轮询 + 接收处理
 * ===================================================================== */
int main(void) {
  uint32_t ret = 0;

  HW_Int();       /* 硬件外设初始化 */
  Delay_Ms(1);    /* 短暂延时，等待外设稳定 */

  /* 开机倒计时 1 秒（最前）：用 SysTick 精确 1 秒间隔 */
  for (int i = 1; i >= 1; i--) {
    printf("Boot countdown: %d s\r\n", i);
    HAL_Delay_nMs(1000); /* 精确延时 1 秒（Delay_Ms 是软件空循环，时长不准） */
  }
  printf("Boot countdown done, starting...\r\n");

  printf_logo();  /* 打印工程信息 */

  /* 初始化射频芯片 */
  ret = rf_init();
  if (ret != RF_OK) {
    printf("[LoRa] init FAIL (ret=%lu), system halted\r\n",
           (unsigned long)ret); /* 射频初始化失败，死循环 */
    while (1)
      ;
  }
  printf("[LoRa] init OK\r\n");
  rf_set_default_para(); /* 设置默认射频参数（频率、功率、扩频因子等） */

  /* 打印当前 LoRa 配置，便于开机核对与发送端是否一致 */
  {
    const char *bwStr;
    const char *crStr;

    switch (DEFAULT_BW) {
      case BW_62_5K: bwStr = "62.5"; break;
      case BW_125K:  bwStr = "125";  break;
      case BW_250K:  bwStr = "250";  break;
      case BW_500K:  bwStr = "500";  break;
      default:       bwStr = "?";    break;
    }
    switch (DEFAULT_CR) {
      case CODE_RATE_45: crStr = "4/5"; break;
      case CODE_RATE_46: crStr = "4/6"; break;
      case CODE_RATE_47: crStr = "4/7"; break;
      case CODE_RATE_48: crStr = "4/8"; break;
      default:           crStr = "?";   break;
    }

    printf("===== LoRa Config =====\r\n");
    printf("  Freq    : %d MHz\r\n", DEFAULT_FREQ / 1000000);
    printf("  SF      : %d\r\n", DEFAULT_SF);
    printf("  BW      : %s kHz\r\n", bwStr);
    printf("  CR      : %s\r\n", crStr);
    printf("  Power   : %d dBm\r\n", DEFAULT_PWR);
    printf("  CRC     : OFF\r\n"); /* rf_set_default_para 内硬编码 CRC_OFF */
    printf("  SyncWord: 0x%02X\r\n", rf_get_syncword()); /* 回读芯片实际值 */
    printf("=======================\r\n");
  }

  dtu_init(); /* 加载 Flash 配置，启动 DTU 查询定时器 */
#if DTU_PROD_MODE
  printf("UART mode: DTU (raw protocol)\r\n");
#else
  printf("UART mode: DEV shared ({usb}=debug, [dtu]=protocol)\r\n");
#endif

  rf_enter_single_timeout_rx(5000); /* 上电进入接收模式 */
  printf("RX mode: concentrator listening...\r\n");

  /* 主循环 */
  while (1) {
    /* DTU 协议轮询：队列发送/帧解析/csq+nettime 周期查询 */
    dtu_poll();

    /* LoRa 接收处理：上报 DTU + 下发响应 */
    OnSlave();
  }
}
