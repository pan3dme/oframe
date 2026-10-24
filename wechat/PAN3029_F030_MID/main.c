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
#include <stdbool.h>

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
static bool     timeSynced = false;   /* 是否已同步时间：false=未同步, true=已同步 */

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
  timeSynced = true;
}

/* ========================== 收发缓冲区定义 ========================== */
#define RX_LEN 64              /* 接收缓冲区长度（字节） */

/* 收到子设备上报后，延时多久再用 LoRa 回复（ms）
 * 共同前提：子设备发完 LoRa 需要从 TX 切回 RX，回复必须落在它的接收窗口内，
 * 太早下发子设备还在 TX 状态会漏收。
 * 两类回复的延时不同，按子设备侧实测的接收窗口取值；改这两个值
 * 必须同步确认子设备侧的 TX→RX 切换耗时。
 *   - 指令类回复（缓存指令 / gpstm 中继）：1000ms
 *   - 对时回复                          ：1500ms */
#define LORA_REPLY_DELAY_CMD_MS      1000
#define LORA_REPLY_DELAY_TIMESYNC_MS 1500

/* 收完 / 发完之后重新进入 LoRa 监听的超时（ms） */
#define LORA_RX_LISTEN_MS 15000

extern struct RxDoneMsg RxDoneParams; /* 接收完成时的消息结构体（由射频驱动提供） */

/* ============ 延时回复调度：中继的常态是"监听"，不是"死等" ============
 * 收到子设备上报后不能用 Delay_Ms() 原地干等下发窗口——那段时间射频不在
 * 监听态，主循环的 dtu_poll() 也被卡住，DTU 下发的指令要等 1~1.5s 才被处理。
 * 改成：
 *   1) OnSlave() 只登记"到点该回什么"，然后立刻回监听；
 *   2) 主循环 lora_reply_process() 发现到点 → 切 TX 发一次 → 发完立刻回监听。
 * 下发窗口仍是 1000 / 1500ms，只是从"阻塞等待"变成"到点触发"。
 * ====================================================================== */
typedef enum {
  LORA_REPLY_NONE = 0,
  LORA_REPLY_PENDING_CMD, /* 回复缓存指令：9|dev|cmd|val */
  LORA_REPLY_UPGPS,       /* 回复 upgps    ：9|dev|upgps|0 */
  LORA_REPLY_TIME_SYNC    /* 回复对时      ：4|ts|name     */
} LoraReplyKind_t;

typedef struct {
  uint8_t         active;  /* 1 = 有待发任务 */
  LoraReplyKind_t kind;
  uint32_t        dueTick; /* 到点时刻（HAL_GetTick()） */
  char            deviceId[32];
} LoraReplyJob_t;

static LoraReplyJob_t g_replyJob; /* 同一时刻只允许 1 个待发任务 */

/* 登记一条延时回复任务 */
static void lora_reply_schedule(LoraReplyKind_t kind, const char *deviceId,
                                uint16_t delayMs) {
  if (g_replyJob.active) {
    /* 窗口已被占用：不覆盖，保证先到的设备一定拿到回复；
     * 后到这台的数据已正常上报 DTU，等它下次上报再排。 */
    printf("[LoRa] 回复窗口占用中(kind=%d,dev=%s)，本次不排程\r\n",
           g_replyJob.kind, g_replyJob.deviceId);
    return;
  }
  g_replyJob.kind = kind;
  g_replyJob.dueTick = HAL_GetTick() + delayMs;
  strncpy(g_replyJob.deviceId, deviceId, sizeof(g_replyJob.deviceId) - 1);
  g_replyJob.deviceId[sizeof(g_replyJob.deviceId) - 1] = '\0';
  g_replyJob.active = 1;
  printf("[LoRa] 排程回复 kind=%d dev=%s +%ums\r\n", kind, g_replyJob.deviceId,
         delayMs);
}

/* 主循环调用：到点就切 TX 下发一次，发完立刻回到监听态 */
static void lora_reply_process(void) {
  LoraReplyKind_t kind;
  char devId[32];

  if (!g_replyJob.active)
    return;
  if ((int32_t)(HAL_GetTick() - g_replyJob.dueTick) < 0)
    return; /* 还没到下发窗口，继续监听 */

  /* 先取副本并清任务：lora_tx_wait() 内部会阻塞，避免期间被重入 */
  kind = g_replyJob.kind;
  memcpy(devId, g_replyJob.deviceId, sizeof(devId));
  g_replyJob.active = 0;

  switch (kind) {
    case LORA_REPLY_PENDING_CMD:
      lora_reply_pending_cmd(devId);
      break;
    case LORA_REPLY_UPGPS:
      lora_reply_upgps(devId);
      break;
    case LORA_REPLY_TIME_SYNC:
      lora_reply_time_sync();
      break;
    default:
      break;
  }

  rf_enter_single_timeout_rx(LORA_RX_LISTEN_MS); /* 发完立刻回到监听态 */
}

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

        /* 上报 DTU：子设备上报的第 3 段（时间字段）有两种可能——
         *   ① 含日期的完整 Unix 时间戳（>=10 位）→ 原样上报
         *   ② 只带"当天 00:00 起的秒数"（如 36000 = 10:00:00）→ 用本机
         *      当天 00:00 补齐成完整时间戳后再上报
         * 补齐失败（格式不符 / 缓冲区不足）时降级为原样上报，不丢数据。 */
        if (msgType == MSG_TYPE_TIME) {
          char reportBuf[RX_LEN];
          if (dtu_replace_time_segment((char *)local_buf, reportBuf,
                                       sizeof(reportBuf)) == 0)
            dtu_report_lora(reportBuf, local_rssi - 256, local_snr);
          else
            dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
        } else {
          dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
        }

        /* 这里只排程，不原地等待（优先级同 CENTEN：缓存指令 > gpstm > 对时）
         * 无缓存指令且无 gpstm 时，无条件回复对时（未同步时下发开机秒数）。
         * 排程后立刻由本函数末尾的 rf_enter_single_timeout_rx() 回到监听态，
         * 到点再由主循环 lora_reply_process() 切 TX 下发。 */
        if (hasPendingCmd) {
          lora_reply_schedule(LORA_REPLY_PENDING_CMD, loraDevId,
                              LORA_REPLY_DELAY_CMD_MS);
        } else if (dtu_gpstm_active()) {
          lora_reply_schedule(LORA_REPLY_UPGPS, loraDevId,
                              LORA_REPLY_DELAY_CMD_MS);
        } else {
         
          lora_reply_schedule(LORA_REPLY_TIME_SYNC, loraDevId,
                              LORA_REPLY_DELAY_TIMESYNC_MS);
        }
      } else if (msgType == MSG_TYPE_CONFIG) {
        /* 配置消息（类型 6）：只做"透传"——原样经 USART1 上报 DTU，
         * 不做任何 LoRa 回复（不对时、不下发指令）。
         * 即子设备发完配置消息后本集中器静默，子设备不应等待回复。 */
        dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
      }
      /* 其余 msgType（如 8=告警）目前既不上报也不回复，直接丢包 */
    }

    /* 处理完这条上报立刻回到监听态（待发回复由主循环到点再切 TX） */
    rf_enter_single_timeout_rx(LORA_RX_LISTEN_MS);
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

    /* LoRa 接收处理：上报 DTU + 排程待发回复 + 回监听 */
    OnSlave();

    /* 到点下发：切 TX 发一次，发完立刻回监听（非阻塞，射频常态保持监听） */
    lora_reply_process();
  }
}
