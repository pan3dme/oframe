/**********************************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All
 * rights reserved.
 *
 * @file main.c
 * @brief PAN3029/PAN3060 LoRa 单工通信演示程序（Simplex）
 *        - TX 模式：每隔 3 分钟上报一帧，发送后开启 3 秒接收窗口并解析一条消息
 *        - RX 模式：持续接收数据，打印 RSSI/SNR/载荷，并与上次接收做比对
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

/* ========================== 消息类型定义（与 ESP32 对齐） ========================== */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报（预留，暂不使用） */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_WARN     8   /* 告警消息 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* ========================== 时间同步变量 ========================== */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间戳 */
static uint32_t syncedTick = 0;       /* 同步时的 HAL_GetTick() 值 */
static uint8_t  timeSynced = 0;       /* 是否已同步时间：0=未同步, 1=已同步 */

/**
 * @brief  获取当前"当天秒数"（从 00:00:00 开始的秒数）
 * @return 当天秒数
 *         - 未同步时：把开机当作 00:00，返回开机秒数
 *         - 已同步后：返回真实时间戳 % 86400
 */
uint32_t getTodaySecond(void) {
  if (!timeSynced) {
    /* 未同步：把开机当作 00:00，返回开机秒数 */
    return HAL_GetTick() / 1000;
  }
  /* 已同步：计算当前时间戳 = 同步时间戳 + 经过的秒数 */
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  uint32_t currentTs = syncedTimestamp + elapsed_sec;
  /* 返回当天秒数（不含年月日） */
  return currentTs % 86400;  /* 86400 = 24 * 60 * 60 */
}

/**
 * @brief  获取当前完整时间戳
 * @return Unix 时间戳
 *         - 未同步时：返回开机秒数（从 1970-01-01 00:00:00 开始）
 *         - 已同步后：返回真实时间戳
 */
uint32_t getCurrentTimestamp(void) {
  if (!timeSynced) {
    return HAL_GetTick() / 1000;  /* 从 1970-01-01 开始 */
  }
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  return syncedTimestamp + elapsed_sec;
}

/**
 * @brief  判断闰年
 */
static uint8_t isLeapYear(uint16_t year) {
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/**
 * @brief  将 Unix 时间戳格式化为 "YYYY-MM-DD HH:MM:SS" 字符串
 * @param  ts    Unix 时间戳
 * @param  buf   输出缓冲区（至少 20 字节）
 * @param  len   缓冲区长度
 */
void formatTimestamp(uint32_t ts, char *buf, uint8_t len) {
  uint32_t sec = ts % 60; ts /= 60;
  uint32_t min = ts % 60; ts /= 60;
  uint32_t hour = ts % 24; ts /= 24;
  
  /* 从 1970-01-01 开始计算年月日 */
  uint16_t year = 1970;
  uint8_t month = 1;
  uint8_t day = 1;
  
  /* 计算年份 */
  while (1) {
    uint16_t days_in_year = isLeapYear(year) ? 366 : 365;
    if (ts < days_in_year) break;
    ts -= days_in_year;
    year++;
  }
  
  /* 计算月份 */
  uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(year)) days_in_month[1] = 29;
  
  while (month <= 12) {
    if (ts < days_in_month[month - 1]) break;
    ts -= days_in_month[month - 1];
    month++;
  }
  day = ts + 1;
  
  snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
           year, month, day, hour, min, sec);
}

/* ========================== 设备信息 ========================== */
#define DEVICE_NAME  "v4-18"   /* 设备名称，每个节点不同 */
#define BATTERY_VAL 100         /* 电量值（无 ADC，固定 100） */

/* ========================== 收发缓冲区定义 ========================== */
#define TX_LEN 64   /* 发送缓冲区长度（字节） */
#define RX_LEN 64   /* 接收缓冲区长度（字节） */

uint8_t tx_test_buf[TX_LEN];                                /* 待发送的数据 */
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
 * @brief  解析接收到的消息（按 '|' 分割字段）
 *         格式：MSG_TYPE|字段1|字段2|...|
 * ===================================================================== */
void parse_rx_message(uint8_t *buf, uint8_t len) {
  char *str = (char *)buf;
  char *fields[8] = {0};
  uint8_t field_count = 0;

  /* 按 '|' 分割，提取各字段 */
  fields[field_count++] = str;
  for (uint8_t i = 0; i < len && field_count < 8; i++) {
    if (str[i] == '|') {
      str[i] = '\0';
      if (i + 1 < len)
        fields[field_count++] = &str[i + 1];
    }
  }

  if (field_count < 2) {
    printf("[parse] invalid msg\r\n");
    return;
  }

  uint8_t msg_type = atoi(fields[0]);

  switch (msg_type) {
    case MSG_TYPE_SYN_TIME: /* 4: 下发对时信息 4|timestamp|xxx */
      if (field_count >= 2) {
        syncedTimestamp = atoi(fields[1]);
        syncedTick = HAL_GetTick();
        timeSynced = 1;
        printf("[SYNC] ts=%lu today=%lu\r\n",
               (unsigned long)syncedTimestamp, (unsigned long)getTodaySecond());
      }
      break;
    case MSG_TYPE_COM:    /* 9: 指令 */
      printf("[CMD] %s\r\n", field_count > 2 ? fields[2] : "");
      break;
    default:
      printf("[MSG] type=%d\r\n", msg_type);
      break;
  }
}

/* =====================================================================
 * @brief  Master（主机/发射端）流程
 *         1. 按 ESP32 格式组包并发送（MSG_TYPE_TIME|设备名|秒数|电量）
 *         2. 发送完成后开启 3 秒接收窗口，只处理一条消息并解析
 *         3. 等待 3 分钟后进入下一次发送
 * ===================================================================== */
void OnMaster(void) {
  static uint32_t tx_count = 0;  /* 步进计数器 */
  char tx_msg[TX_LEN];
  uint32_t today_sec = getTodaySecond(); /* 获取当天秒数（已同步则用真实时间） */

  tx_count++;
  /* 按 ESP32 格式组包：2|f030-1|12345|100|1 */
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%lu|%d|%lu",
           MSG_TYPE_TIME, DEVICE_NAME, (unsigned long)today_sec, BATTERY_VAL, (unsigned long)tx_count);

  /* 发送数据 */
  if (rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time) != OK) {
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); /* 发送失败，LED 熄灭 */
  }

  /* 等待发送完成标志 */
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE); /* 清除发送完成标志 */

  LedToggle();
  printf("tx ok -> \"%s\"\r\n", tx_msg);

  /* ---- 开启 3 秒接收窗口（single 模式，最多只收一条） ---- */
  rf_enter_single_timeout_rx(3000);
  printf("[RX window] 3s listening...\r\n");

  /* 等待接收完成或超时（收到消息会立刻退出，不会等满 3 秒） */
  while (rf_get_recv_flag() == RADIO_FLAG_IDLE)
    ;

  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
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

    /* 确保字符串结束 */
    if (local_size < RX_LEN)
      local_buf[local_size] = '\0';
    else
      local_buf[RX_LEN - 1] = '\0';

    /* 打印接收数据（精简版，适配 115200） */
    printf("[RX] len=%d rssi=%d snr=%d data=%s\r\n",
           local_size, local_rssi - 256, local_snr, (char *)local_buf);

    /* 解析消息 */
    parse_rx_message(local_buf, local_size);
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[RX window] timeout, no data.\r\n");
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[RX window] crc error.\r\n");
  }

  /* 调试模式：不进入休眠，打印当前时间和倒计时 */
  /* rf_set_mode(RF_MODE_STB1); */
  /* printf("[RF] enter standby\r\n"); */

  /* 等待 1 分钟，每秒打印当前时间和倒计时 */
  printf("[DEBUG] 等待下次上报...\r\n");
  for (int i = 60; i > 0; i--) {
    HAL_Delay_nMs(1000);
    char time_str[20];
    formatTimestamp(getCurrentTimestamp(), time_str, sizeof(time_str));
    printf("[DEBUG] %s  倒计时: %d s\r\n", time_str, i);
  }
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
      printf("  [SAME]"); /* 与上次相同 */
    else
      printf("  [DIFF]"); /* 与上次不同 */
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
  // rf_set_crc(true);      /* 开启 CRC 校验 */

  /* 默认进入发送模式 */
  current_mode = 1;
  printf("TX mode: transmit only.\r\n");

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
