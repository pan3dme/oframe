/* PAN3029 LoRa 集中器
 * 功能：LoRa 收子设备上报 → USART1 以 JSON 上报 DTU；
 *       DTU 下发对时/配置/指令缓存/gpstm 中继；
 *       收到上报后延时回复：缓存指令 > gpstm(upgps) > 对时。
 * 硬件：STM32F030 + PAN3029/3060，USART1(PA9/PA10) 调试与 DTU 共用，115200 8N1
 *       DTU 协议永远原样收发、不加前缀；调试打印每行带 {usb} 前缀
 *       usbDebugPrint=true 输出调试信息, false 只跑协议
 * V3.0 由 CENTEN 迁移，移除旧 TX/RX 双模式
 */

#include "delay.h"
#include "stm32f0xx.h"
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define EXAMPLE_NAME "stm32集中器"
#define DEMO_VER "V3.0"
#define EXAMPLE_DATE "2026/10/23"

/* 串口打印启动信息 */
void printf_logo(void) {
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* 工程名称        : %s\r\n", EXAMPLE_NAME);
  printf("* 固件版本        : %s\r\n", DEMO_VER);
  printf("* 编译日期        : %s\r\n", EXAMPLE_DATE);
  printf("* www.silicontra.com 矽特诺微电子有限公司\r\n");
  printf("*************************************************************\r\n");
}

/* ================= 设备名：按芯片 UID 分配编号 =================
 * 表项 {UID[95:64], UID[63:32], UID[31:0]}，即开机打印的高-中-低。
 * 槽位 i 命中 → 设备名 "v4-" + (100+i)，即 v4-100 ~ v4-109；全 0 槽位跳过；
 * 未命中（表为空或换板）保持默认名 DEVICE_NAME_DEFAULT，云端不受影响。
 * 填表：看开机日志 "Chip UID : AAAAAAAA-BBBBBBBB-CCCCCCCC"，按 A-B-C 填入槽位 */
#define DEVICE_UID_SLOTS 10
static const uint32_t deviceUidList[DEVICE_UID_SLOTS][3] = {
    {0x00000000, 0x00000000, 0x00000000}, /* 0 */
    {0x00000000, 0x00000000, 0x00000000}, /* 1 */
    {0x00000000, 0x00000000, 0x00000000}, /* 2 */
    {0x00000000, 0x00000000, 0x00000000}, /* 3 */
    {0x00000000, 0x00000000, 0x00000000}, /* 4 */
    {0x00000000, 0x00000000, 0x00000000}, /* 5 */
    {0x00000000, 0x00000000, 0x00000000}, /* 6 */
    {0x00000000, 0x00000000, 0x00000000}, /* 7 */
    {0x00000000, 0x00000000, 0x00000000}, /* 8 */
    {0x00000000, 0x00000000, 0x00000000}, /* 9 */
};

/* STM32F030 UID 地址（F103 是 0x1FFFF7E8/EC/F0，不能混用） */
static int getDeviceIndex(void) {
  uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7AC;
  uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7B0;
  uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7B4;

  for (int i = 0; i < DEVICE_UID_SLOTS; i++) {
    if (deviceUidList[i][0] == 0 && deviceUidList[i][1] == 0 &&
        deviceUidList[i][2] == 0)
      continue; /* 空槽位 */
    if (deviceUidList[i][0] == uid2 && deviceUidList[i][1] == uid1 &&
        deviceUidList[i][2] == uid0)
      return i;
  }
  return -1;
}

/* 打印芯片 UID，并按 UID 表设置 deviceName（未命中保持默认名） */
static void device_name_init(void) {
  uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7AC;
  uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7B0;
  uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7B4;
  int idx;

  printf("Chip UID    : %08X-%08X-%08X\r\n", (unsigned int)uid2,
         (unsigned int)uid1, (unsigned int)uid0);

  idx = getDeviceIndex();
  if (idx >= 0) {
    snprintf(deviceName, sizeof(deviceName), "v4-%d", 100 + idx);
    printf("设备名称    : %s (index=%d)\r\n", deviceName, 100 + idx);
  } else {
    printf("设备名称    : %s (UID 不在表内, 用默认名)\r\n", deviceName);
  }
}

/* 时间同步：DTU 网络时间 + 同步时刻 */
static uint32_t syncedTimestamp = 0;
static uint32_t syncedTick = 0;
static bool     timeSynced = false;

/* 当前 Unix 时间戳，未同步时返回开机秒数 */
uint32_t getCurrentTimestamp(void) {
  if (!timeSynced) {
    return HAL_GetTick() / 1000;
  }
  uint32_t elapsed_sec = (HAL_GetTick() - syncedTick) / 1000;
  return syncedTimestamp + elapsed_sec;
}

/* 写入主时间源（dtu.c 对时成功后调用） */
void app_set_synced_time(uint32_t ts) {
  syncedTimestamp = ts;
  syncedTick = HAL_GetTick();
  timeSynced = true;
}

#define RX_LEN 64 /* 接收缓冲区长度 */

/* 收到上报后延时回复（ms）：子设备发完需从 TX 切回 RX，
 * 回复必须落在其接收窗口内，改值须同步确认子设备侧切换耗时 */
#define LORA_REPLY_DELAY_CMD_MS      1000 /* 指令类回复 */
#define LORA_REPLY_DELAY_TIMESYNC_MS 1500 /* 对时回复 */

#define LORA_RX_LISTEN_MS 15000 /* 空闲监听超时 */

extern struct RxDoneMsg RxDoneParams;

/* 上次收到的帧，仅用于调试打印 [SAME]/[DIFF]，不影响上报 */
static uint8_t prev_rx_buf[RX_LEN];
static uint8_t prev_rx_size = 0;

/* LED1 接收指示：点亮后由主循环 500ms 熄灭（非阻塞） */
static uint8_t  led1On = 0;
static uint32_t led1OnTick = 0;

/* 延时回复调度：OnSlave() 只登记任务并立刻回监听，
 * 主循环到点再切 TX 发一次，避免 Delay_Ms 阻塞 dtu_poll() 与射频监听 */
typedef enum {
  LORA_REPLY_NONE = 0,
  LORA_REPLY_PENDING_CMD, /* 9|dev|cmd|val */
  LORA_REPLY_UPGPS,       /* 9|dev|upgps|0 */
  LORA_REPLY_TIME_SYNC    /* 4|ts|name    */
} LoraReplyKind_t;

typedef struct {
  uint8_t         active;
  LoraReplyKind_t kind;
  uint32_t        dueTick;
  char            deviceId[32];
} LoraReplyJob_t;

static LoraReplyJob_t g_replyJob; /* 同一时刻只允许 1 个待发任务 */

/* 射频开关：loraSwOn 只存 RAM，重启即恢复收发，避免关掉后醒不过来。
 * 工作时段优先：时段外一律休眠且拒绝 lorasw|1；
 * 时段内手动关闭不会被强制重开（边沿触发） */
static uint8_t  loraSwOn = 1;
static uint8_t  prevInWorkWindow = 1;
static uint32_t workWinCheckTick = 0;
static uint8_t  loraWakeFail = 0;      /* 上次唤醒失败（仅此情况自动重试） */
static uint32_t loraWakeRetryTick = 0;

/* 登记延时回复任务；窗口已被占用则不覆盖，保证先到的设备拿到回复 */
static void lora_reply_schedule(LoraReplyKind_t kind, const char *deviceId,
                                uint16_t delayMs) {
  if (g_replyJob.active) {
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

/* 到点切 TX 下发一次，发完回监听 */
static void lora_reply_process(void) {
  LoraReplyKind_t kind;
  char devId[32];

  /* 射频休眠时不发射，并清掉待发任务，避免唤醒后补发过期回复 */
  if (!loraSwOn) {
    g_replyJob.active = 0;
    return;
  }

  if (!g_replyJob.active)
    return;
  if ((int32_t)(HAL_GetTick() - g_replyJob.dueTick) < 0)
    return; /* 未到下发窗口 */

  /* 先取副本并清任务：lora_tx_wait() 内阻塞，避免重入 */
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

  rf_enter_single_timeout_rx(LORA_RX_LISTEN_MS); /* 发完回监听 */
}

/* 系统时钟 48MHz：HSI 8MHz/2 = 4MHz → PLL x12 */
void SysClock_48() {
  RCC_PLLCmd(DISABLE);
  RCC_PLLConfig(RCC_PLLSource_HSI_Div2, RCC_PLLMul_12);
  RCC_PLLCmd(ENABLE);
  while (!RCC_GetFlagStatus(RCC_FLAG_PLLRDY)) /* 等 PLL 锁定 */
    ;
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
}

/* SysTick 1ms 中断 */
void Tick_Configration() {
  if (SysTick_Config(SystemCoreClock / 1000)) {
    while (1)
      ;
  }
}

/* 开启 GPIO / SPI2 / USART1 / SYSCFG 时钟 */
void RCC_Configuration() {
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB |
                            RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOF,
                        ENABLE);

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2 | RCC_APB1Periph_PWR, ENABLE);

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_SYSCFG, ENABLE);
}

/* EXTI Line1(PA1)：射频 DIO1 上升沿，收发完成事件 */
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

/* 硬件初始化：时钟 → SysTick → 外设时钟 → GPIO → SPI → UART → NVIC */
void HW_Int()
{
  SysClock_48();
  Tick_Configration();
  RCC_Configuration();
  GPIO_int();
  SPI2_Int();
  Uart1_Int();   /* USART1：调试打印 + DTU 协议 */
  NVIC_Config();
}

/* LED 闪一次（亮 50ms → 灭 50ms） */
void LedToggle(void) {
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET);
  HAL_Delay_nMs(50);
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
  HAL_Delay_nMs(50);
}

/* 收到数据：LED 亮，由 led_poll() 在 500ms 后熄灭（不阻塞主循环） */
static void led_on_500ms(void) {
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET); /* 低电平点亮 */
  led1OnTick = HAL_GetTick();
  led1On = 1;
}

static void led_poll(void) {
  if (led1On && HAL_GetTick() - led1OnTick >= 500) {
    led1On = 0;
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
  }
}

/* 复位键轮询：S2(KEY1/PB6) 低电平 → 20ms 消抖 → 重启 MCU
 * 底板 S1=NRST 硬件复位，S3=PB7 与 LED2 复用不处理 */
static void key_reset_poll(void) {
  static uint8_t  pressed   = 0;
  static uint32_t pressTick = 0;

  if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == Bit_RESET) { /* 低=按下 */
    if (!pressed) {
      pressed   = 1;
      pressTick = HAL_GetTick();
    } else if (HAL_GetTick() - pressTick >= 20) {
      GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET);
      printf("[KEY] 复位键(S2/PB6)按下, 重启 MCU\r\n");
      Delay_Ms(200);      /* 等日志吐完 */
      NVIC_SystemReset(); /* noreturn */
    }
  } else {
    pressed = 0;
  }
}

/* PAN3029 硬件复位：NRST(PB0) 拉低 20ms → 拉高 → 等 50ms 稳定。
 * 手册要求 ≥10ms，留余量避免 NRST 上并电容时充不满导致 rf_init 失败 */
static void lora_hw_reset(void) {
  GPIO_WriteBit(RF_RST_PORT, RF_RST_IO, Bit_RESET);
  Delay_Ms(20);
  GPIO_WriteBit(RF_RST_PORT, RF_RST_IO, Bit_SET);
  Delay_Ms(50);
  printf("[LoRa] NRST(PB0) reset pulse done\r\n");
}

/* 关射频深度休眠：MCU 与 DTU 继续工作，仍可经 DTU 收 lorasw|1 唤醒。
 * 这里不能动 DTU/串口，否则关掉后收不到唤醒指令 */
static void lora_rf_sleep(void) {
  rf_deepsleep();
  loraSwOn = 0;
  GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); /* LED 灭 */
  printf("[LoRa开关] 射频已关闭(深度休眠), MCU与DTU仍工作\r\n");
}

/* 唤醒射频并恢复监听。1=成功, 0=失败（保持休眠，等时段边沿或 60s 重试）
 * 深度休眠后寄存器掉电，必须重跑 rf_init + rf_set_default_para */
static uint8_t lora_rf_wake(void) {
  uint8_t retry = 0;

  lora_hw_reset();

  while (rf_init() != RF_OK) {
    if (retry >= 2) {
      printf("[LoRa开关] 射频唤醒失败(init), 保持休眠, 60秒后重试\r\n");
      loraSwOn = 0;
      loraWakeFail = 1;
      loraWakeRetryTick = HAL_GetTick();
      return 0;
    }
    retry++;
    printf("[LoRa开关] 唤醒初始化失败, 复位后重试(%d)\r\n", retry);
    lora_hw_reset();
  }

  rf_set_default_para();
  rf_enter_single_timeout_rx(LORA_RX_LISTEN_MS);
  loraSwOn = 1;
  loraWakeFail = 0;
  printf("[LoRa开关] 射频已开启, 恢复监听\r\n");
  return 1;
}

/* lorasw 指令入口：1=唤醒收发, 0=深度休眠 */
void app_lora_switch(uint8_t on) {
  if (on) {
    if (!loraSwOn)
      lora_rf_wake();
  } else {
    if (loraSwOn)
      lora_rf_sleep();
  }
}

/* 当前射频状态：1=开, 0=休眠 */
uint8_t app_lora_is_on(void) { return loraSwOn; }

/* 工作时段轮询：每秒一次，只在进出时段的边沿切射频。
 * 未对时 / 配置无时段字段时 dtu_in_work_window() 恒为 1（常开）；
 * 时段内唤醒失败每 60s 重试，但不覆盖手动 lorasw|0 */
static void work_window_poll(void) {
  uint8_t inWin;

  if (HAL_GetTick() - workWinCheckTick < 1000)
    return;
  workWinCheckTick = HAL_GetTick();

  inWin = dtu_in_work_window();
  if (inWin != prevInWorkWindow) {
    prevInWorkWindow = inWin;
    if (inWin) {
      printf("[工作时间] 进入工作时段, 射频唤醒\r\n");
      if (!loraSwOn)
        lora_rf_wake();
    } else {
      printf("[工作时间] 离开工作时段, 射频休眠\r\n");
      if (loraSwOn)
        lora_rf_sleep();
    }
    return;
  }

  if (inWin && !loraSwOn && loraWakeFail &&
      HAL_GetTick() - loraWakeRetryTick >= 60000) {
    loraWakeRetryTick = HAL_GetTick();
    printf("[工作时间] 射频唤醒重试\r\n");
    lora_rf_wake();
  }
}

/* 集中器接收处理：
 * RXDONE   ：上报 DTU 后按优先级排程回复（缓存指令 > gpstm > 对时）
 * RXERR    ：CRC 关闭时数据仍可读，照常打印
 * RXTIMEOUT：重新接收 */
void OnSlave(void) {
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN];
    uint8_t local_size;
    uint8_t local_rssi;
    uint8_t local_snr;

    /* 关中断快速拷贝，避免被中断覆盖 */
    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    local_size = RxDoneParams.Size;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    /* 与上一帧比对，仅用于调试打印 [SAME]/[DIFF] */
    {
      uint8_t is_same = 0;

      if (local_size == prev_rx_size && prev_rx_size > 0) {
        is_same = 1;
        for (uint8_t i = 0; i < local_size; i++) {
          if (local_buf[i] != prev_rx_buf[i]) {
            is_same = 0;
            break;
          }
        }
      }

      printf("Rssi: %d  ", local_rssi - 256); /* RSSI 减 256 得实际 dBm */
      printf("Snr: %d   ", local_snr);
      printf("Len: %d  ", local_size);
      printf("Str: ");
      for (uint8_t i = 0; i < local_size; i++) {
        if (local_buf[i] == 0) break;
        printf("%c", local_buf[i]);
      }
      printf("  [%s]\r\n", is_same ? "SAME" : "DIFF");

      prev_rx_size = local_size;
      for (uint8_t i = 0; i < local_size; i++)
        prev_rx_buf[i] = local_buf[i];
    }

    led_on_500ms();

    {
      int msgType;
      char loraDevId[32] = {0};
      uint8_t hasPendingCmd = 0;

      if (local_size < RX_LEN)
        local_buf[local_size] = '\0';
      msgType = atoi((char *)local_buf);
      printf("[LoRa] MsgType: %d\r\n", msgType);

      /* GPS(1) / UP_GPS(5) / TIME(2) 都上报 DTU */
      if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_UP_GPS ||
          msgType == MSG_TYPE_TIME) {
        /* 取 deviceId（第 2 段），查缓存指令 */
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

        /* TIME 第 3 段：完整时间戳原样上报；当天秒数用本机 00:00 补齐，
         * 失败则降级为原样上报 */
        if (msgType == MSG_TYPE_TIME) {
          char reportBuf[RX_LEN];
          if (dtu_replace_time_segment((char *)local_buf, reportBuf,
                                       sizeof(reportBuf)) == 0)
            dtu_report_lora(reportBuf, local_rssi - 256, local_snr);
          else
            dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
        } else {
          /* GPS/UP_GPS 的第 3 段可能是压缩坐标（相对农场中心，1e-5 度），
           * 能还原就还原，否则原样上报，绝不丢数据 */
          char coordBuf[RX_LEN + 16];
          if (dtu_expand_coord((char *)local_buf, coordBuf,
                               sizeof(coordBuf)) == 0) {
            printf("[LoRa] 坐标还原: %s -> %s\r\n", (char *)local_buf, coordBuf);
            dtu_report_lora(coordBuf, local_rssi - 256, local_snr);
          } else {
            dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
          }
        }

        /* 只排程不等待，优先级同 CENTEN：缓存指令 > gpstm > 对时。
         * UP_GPS(5) 只上报不回复：子设备发完不开接收窗口，回复白占窗口 */
        if (msgType == MSG_TYPE_UP_GPS) {
          /* 无回复 */
        } else if (hasPendingCmd) {
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
        /* 配置消息(6)：原样透传上报，不做任何 LoRa 回复 */
        dtu_report_lora((char *)local_buf, local_rssi - 256, local_snr);
      }
      /* 其余 msgType（如 8=告警）直接丢弃 */
    }

    rf_enter_single_timeout_rx(LORA_RX_LISTEN_MS); /* 回到监听 */
  }

  /* CRC 标志：CRC 关闭时数据仍可读，照常打印 */
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
    rf_enter_single_timeout_rx(5000);
  }

  /* 接收超时 */
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    printf("rx time out\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000);
  }
}

/* 主函数：初始化硬件/射频/DTU，然后循环轮询 */
int main(void) {
  uint32_t ret = 0;

  /* 调试打印开关：true = 串口输出 {usb} 调试信息, false = 只跑 DTU 协议 */
  usbDebugPrint = false;

  HW_Int();
  Delay_Ms(1);

  /* 开机倒计时 1 秒 */
  for (int i = 1; i >= 1; i--) {
    printf("Boot countdown: %d s\r\n", i);
    HAL_Delay_nMs(1000); /* Delay_Ms 是软件空循环，时长不准 */
  }
  printf("Boot countdown done, starting...\r\n");

  printf_logo();

  /* 打印芯片 UID，按 UID 表决定设备名（未命中保持默认名） */
  device_name_init();

  /* 先硬件复位射频再 SPI 初始化，避免芯片停在上一次的半初始化状态 */
  lora_hw_reset();

  ret = rf_init();
  if (ret != RF_OK) {
    /* 失败：LED 常亮 → 复位射频 → 延时 3s → 复位 MCU。
     * 硬件真坏时表现为每 3s 重启并反复打印 init FAIL，便于定位 */
    printf("[LoRa] init FAIL (ret=%lu), reset MCU in 3s\r\n",
           (unsigned long)ret);
    GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET);
    lora_hw_reset();
    Delay_Ms(3000);
    NVIC_SystemReset(); /* noreturn */
  }
  printf("[LoRa] init OK\r\n");
  rf_set_default_para();

  /* 打印 LoRa 参数，便于核对与发送端是否一致 */
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

  dtu_init();

  /* 记录上电时是否在时段内（未对时恒为 1=常开）并打印生效区间 */
  prevInWorkWindow = dtu_in_work_window();
  dtu_print_work_window("时段  : ");
  printf("UART mode: DTU raw, debug print %s\r\n",
         usbDebugPrint ? "ON ({usb})" : "OFF");

  rf_enter_single_timeout_rx(5000);
  printf("RX mode: concentrator listening...\r\n");

  while (1) {
    key_reset_poll();   /* 复位键轮询 */

    led_poll();         /* LED 500ms 熄灭 */

    /* 射频休眠期间 DTU 也要工作，否则收不到 lorasw|1 */
    dtu_poll();

    work_window_poll(); /* 工作时段边沿切射频 */

    if (loraSwOn)       /* 休眠时跳过，避免处理残留收发标志 */
      OnSlave();

    lora_reply_process(); /* 到点下发回复 */
  }
}
