/**********************************************************************************************
 * @file dtu.c
 * @brief DTU 协议层（自 STM32_LORA_CENTEN 移植，适配 STM32F030 StdPeriph 库）
 *        - 与调试串口共用 USART1，由 dtu.h 中 SERIAL_MODE 编译期标记选择用途
 *        - DTU 模式功能：JSON 上报 / csq+nettime 周期查询 / JSON 指令解析 /
 *          待执行指令缓存 / gpstm 中继 / Flash 配置存储
 ********************************************************************************************/
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================== DTU 发送队列：环形缓冲区 ========================== */
#define DTU_QUEUE_SIZE    5
#define DTU_MSG_MAX_LEN   256

typedef struct {
  char buf[DTU_MSG_MAX_LEN];
  uint16_t len;
} DtuQueueItem_t;

static DtuQueueItem_t dtuQueue[DTU_QUEUE_SIZE];
static uint8_t  dtuQueueHead = 0;
static uint8_t  dtuQueueTail = 0;
static uint8_t  dtuQueueCount = 0;
static uint32_t dtuLastSendTick = 0;

/* ========================== DTU 接收缓冲区 ========================== */
#define DTU_RX_BUF_SIZE 256
static uint8_t  dtuRxBuf[DTU_RX_BUF_SIZE];
static uint16_t dtuRxLen = 0;
static uint32_t dtuLastRxTick = 0;

/* ========================== 查询 / 时间状态 ========================== */
static uint32_t dtuCsqTick = 0;          /* 周期查询定时器 */
static uint8_t  dtuTimeValid = 0;        /* DTU 网络时间是否已获取 */
static uint8_t  dtuNetTimeFailCount = 0; /* 连续失败计数（5 次重启） */
static int      dtuCsqRssi = 0;          /* DTU CSQ 信号强度 */
static uint32_t rtcSendCount = 0;        /* 对时上报计数器 */
static int      batteryValue = 100;      /* 电池电量（%） */
static uint8_t  configBootSent = 0;      /* 开机配置是否已上报 */

/* ========================== Flash 配置存储（F030C8 最后一页 1KB） ========================== */
#define CONFIG_FLASH_ADDR 0x0800FC00
#define CONFIG_MAGIC      0xA5A5A5A5
static char configStr[32] = "30,0M";

/* ========================== 待执行指令缓存 ========================== */
#define PENDING_CMD_MAX_SLOTS 8

typedef struct {
  uint8_t used;
  char deviceId[32];
  char cmd[32];
  char val[32];
} PendingCmd_t;

static PendingCmd_t pendingCmdCache[PENDING_CMD_MAX_SLOTS];

/* ========================== gpstm 中继状态 ========================== */
static uint8_t  gpstmActive = 0;
static uint32_t gpstmRecvTick = 0;
static uint32_t gpstmTimeoutMin = 0;

/* =====================================================================
 * @brief  LoRa 发送并等待完成（3 秒超时保护）
 * ===================================================================== */
static void lora_tx_wait(void) {
  uint32_t waitStart = HAL_GetTick();
  while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE) {
    if (HAL_GetTick() - waitStart > 3000)
      break;
  }
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
}

/* =====================================================================
 * @brief  将消息加入 DTU 发送队列（队列满时丢弃最早一条）
 * ===================================================================== */
static void enqueueDtuMsg(const char *msg, int len) {
  if (len <= 0 || len >= DTU_MSG_MAX_LEN)
    return;

  __disable_irq();
  if (dtuQueueCount >= DTU_QUEUE_SIZE) {
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
  }
  memcpy(dtuQueue[dtuQueueTail].buf, msg, len);
  dtuQueue[dtuQueueTail].buf[len] = '\0';
  dtuQueue[dtuQueueTail].len = (uint16_t)len;
  dtuQueueTail = (dtuQueueTail + 1) % DTU_QUEUE_SIZE;
  dtuQueueCount++;
  __enable_irq();
}

/* =====================================================================
 * @brief  协议流发送：开发阶段加 [dtu] 前缀，与 {usb} 调试打印区分；
 *         DTU 正式模式原样发送
 * ===================================================================== */
static void dtu_wire_send(const uint8_t *p, uint16_t len) {
#if (SERIAL_MODE != SERIAL_MODE_DTU)
  Uart1_SendBytes((const uint8_t *)"[dtu] ", 6);
#endif
  Uart1_SendBytes(p, len);
}

/* =====================================================================
 * @brief  处理 DTU 发送队列：每 500ms 从队头取出一条发送
 * ===================================================================== */
static void processDtuQueue(void) {
  if (dtuQueueCount == 0)
    return;

  if (HAL_GetTick() - dtuLastSendTick >= 500) {
    dtuLastSendTick = HAL_GetTick();
    DtuQueueItem_t *item = &dtuQueue[dtuQueueHead];
    dtu_wire_send((const uint8_t *)item->buf, item->len);
    Uart1_SendBytes((const uint8_t *)"\r\n", 2);
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
  }
}

/* =====================================================================
 * @brief  统一上报数据给 DTU（JSON 格式，入队）
 * ===================================================================== */
void dtu_report_lora(const char *loraStr, int rssi, int snr) {
  char txBuf[DTU_MSG_MAX_LEN];
  int len = snprintf(txBuf, sizeof(txBuf),
                     "{"
                     "\"version\":\"1.0\","
                     "\"method\":\"thing.event.property.post\","
                     "\"params\":{"
                     "\"lorainfo\":\"%s\","
                     "\"upDateDevice\":\"%s\","
                     "\"tm\":%lu,"
                     "\"rssi\":%d,"
                     "\"snr\":%d"
                     "}"
                     "}",
                     loraStr, DEVICE_NAME,
                     (unsigned long)getCurrentTimestamp(), rssi, snr);

  if (len > 0 && len < (int)sizeof(txBuf))
    enqueueDtuMsg(txBuf, len);
}

/* =====================================================================
 * @brief  获取到网络时间后，上报一次对时信息给 DTU
 *         格式: 2|deviceName|timestamp|battery|count
 * ===================================================================== */
static void sendTimeSyncToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%lu|%d|%lu", MSG_TYPE_TIME,
           DEVICE_NAME, (unsigned long)getCurrentTimestamp(), batteryValue,
           (unsigned long)rtcSendCount++);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* =====================================================================
 * @brief  开机网络正常后，上报一次配置信息给 DTU
 *         格式: 6|deviceName|config_str|count
 * ===================================================================== */
static void sendConfigToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|%lu", MSG_TYPE_CONFIG,
           DEVICE_NAME, configStr, (unsigned long)rtcSendCount++);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* =====================================================================
 * @brief  从 Flash 加载配置信息
 * ===================================================================== */
static void loadConfigFromFlash(void) {
  uint32_t magic = *(volatile uint32_t *)CONFIG_FLASH_ADDR;
  if (magic == CONFIG_MAGIC) {
    const char *p = (const char *)(CONFIG_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(configStr) - 1 && p[i] != '\0'; i++)
      configStr[i] = p[i];
    configStr[i] = '\0';
    printf("[Flash] 加载配置: %s\r\n", configStr);
  } else {
    printf("[Flash] 无有效配置，使用默认: %s\r\n", configStr);
  }
}

/* =====================================================================
 * @brief  保存配置信息到 Flash
 * ===================================================================== */
static void saveConfigToFlash(const char *str) {
  uint8_t i;

  FLASH_Unlock();
  FLASH_ErasePage(CONFIG_FLASH_ADDR);
  FLASH_ProgramWord(CONFIG_FLASH_ADDR, CONFIG_MAGIC);
  for (i = 0; i < strlen(str) + 1; i += 2) {
    uint16_t halfWord = str[i] | (str[i + 1] << 8);
    FLASH_ProgramHalfWord(CONFIG_FLASH_ADDR + 4 + i, halfWord);
  }
  FLASH_Lock();
  printf("[Flash] Config saved: %s\r\n", str);
}

/* =====================================================================
 * @brief  从 configStr 解析对时间隔（毫秒），限制 5~60 分钟
 * ===================================================================== */
static uint32_t getConfigIntervalMs(void) {
  int minutes = atoi(configStr);
  if (minutes < 5)
    minutes = 5;
  if (minutes > 60)
    minutes = 60;
  return (uint32_t)minutes * 60 * 1000;
}

/* =====================================================================
 * @brief  将日期时间转换为 Unix 时间戳（秒）
 * ===================================================================== */
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour,
                                    int min, int sec) {
  static const int daysBeforeMonth[] = {0,   0,   31,  59,  90,  120, 151,
                                        181, 212, 243, 273, 304, 334};
  uint32_t days, timestamp;
  int y;

  y = year - 1;
  days = (uint32_t)(year - 1970) * 365 + (y / 4 - y / 100 + y / 400) -
         (1969 / 4 - 1969 / 100 + 1969 / 400) + daysBeforeMonth[month] + day -
         1;
  if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days++;
  timestamp = days * 86400UL + (uint32_t)hour * 3600 + (uint32_t)min * 60 +
              (uint32_t)sec;
  return timestamp;
}

/* =====================================================================
 * @brief  缓存待执行指令（每个设备最多一条，新指令替换旧指令）
 * ===================================================================== */
static void cachePendingCommand(const char *deviceId, const char *cmd,
                                const char *val) {
  int freeSlot = -1;

  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      strncpy(pendingCmdCache[i].cmd, cmd, sizeof(pendingCmdCache[i].cmd) - 1);
      pendingCmdCache[i].cmd[sizeof(pendingCmdCache[i].cmd) - 1] = '\0';
      strncpy(pendingCmdCache[i].val, val, sizeof(pendingCmdCache[i].val) - 1);
      pendingCmdCache[i].val[sizeof(pendingCmdCache[i].val) - 1] = '\0';
      printf("[PENDING CMD] Replaced: device=%s, cmd=%s, val=%s\r\n", deviceId,
             cmd, val);
      return;
    }
    if (!pendingCmdCache[i].used && freeSlot < 0)
      freeSlot = i;
  }

  if (freeSlot >= 0) {
    pendingCmdCache[freeSlot].used = 1;
    strncpy(pendingCmdCache[freeSlot].deviceId, deviceId,
            sizeof(pendingCmdCache[freeSlot].deviceId) - 1);
    pendingCmdCache[freeSlot]
        .deviceId[sizeof(pendingCmdCache[freeSlot].deviceId) - 1] = '\0';
    strncpy(pendingCmdCache[freeSlot].cmd, cmd,
            sizeof(pendingCmdCache[freeSlot].cmd) - 1);
    pendingCmdCache[freeSlot].cmd[sizeof(pendingCmdCache[freeSlot].cmd) - 1] =
        '\0';
    strncpy(pendingCmdCache[freeSlot].val, val,
            sizeof(pendingCmdCache[freeSlot].val) - 1);
    pendingCmdCache[freeSlot].val[sizeof(pendingCmdCache[freeSlot].val) - 1] =
        '\0';
    printf("[PENDING CMD] Cached: device=%s, cmd=%s, val=%s\r\n", deviceId, cmd,
           val);
  } else {
    printf("[PENDING CMD] Cache full! Cannot cache cmd for device=%s\r\n",
           deviceId);
  }
}

/* =====================================================================
 * @brief  检查指定设备是否有缓存指令，返回 1=有, 0=无
 * ===================================================================== */
uint8_t dtu_check_pending(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0)
      return 1;
  }
  return 0;
}

/* =====================================================================
 * @brief  通过 LoRa 发送缓存指令给目标设备并清除缓存
 *         格式: 9|deviceId|cmd|val
 * ===================================================================== */
uint8_t dtu_send_pending(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      char txData[64];
      int txLen = snprintf(txData, sizeof(txData), "%d|%s|%s|%s", MSG_TYPE_COM,
                           pendingCmdCache[i].deviceId, pendingCmdCache[i].cmd,
                           pendingCmdCache[i].val);
      uint32_t tx_time = 0;
      rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
      printf("[PENDING CMD] LoRa TX: %s\r\n", txData);
      lora_tx_wait();

      memset(&pendingCmdCache[i], 0, sizeof(PendingCmd_t));
      return 1;
    }
  }
  return 0;
}

/* =====================================================================
 * @brief  gpstm 中继是否在有效期内
 * ===================================================================== */
uint8_t dtu_gpstm_active(void) {
  if (!gpstmActive)
    return 0;
  return (HAL_GetTick() - gpstmRecvTick) < gpstmTimeoutMin * 60 * 1000 ? 1 : 0;
}

/* =====================================================================
 * @brief  LoRa 下发 upgps 指令：9|deviceId|upgps|0
 * ===================================================================== */
void dtu_send_upgps(const char *deviceId) {
  char txData[64];
  int txLen = snprintf(txData, sizeof(txData), "%d|%s|upgps|0", MSG_TYPE_COM,
                       deviceId);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa gpstm 中继] %s\r\n", txData);
  lora_tx_wait();
}

/* =====================================================================
 * @brief  LoRa 下发对时消息：4|timestamp|deviceName
 * ===================================================================== */
void dtu_send_time_reply(void) {
  char txData[64];
  int txLen = snprintf(txData, sizeof(txData), "%d|%lu|%s", MSG_TYPE_SYN_TIME,
                       (unsigned long)getCurrentTimestamp(), DEVICE_NAME);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa 下发对时] %s\r\n", txData);
  lora_tx_wait();
}

/* =====================================================================
 * @brief  替换上报数据中的时间字段（第 3 个 '|' 分隔段）为当前时间戳
 * @retval 0=成功, -1=格式不符或缓冲区不足
 * ===================================================================== */
int dtu_replace_time_segment(const char *src, char *dst, int dstSize) {
  const char *p1 = strchr(src, '|');
  if (!p1)
    return -1;
  const char *p2 = strchr(p1 + 1, '|');
  if (!p2)
    return -1;

  int head = (int)(p2 - src); /* 含第二个 '|' 之前的内容 */
  const char *p3 = strchr(p2 + 1, '|');
  const char *tail = p3 ? p3 : (src + strlen(src));

  char tsStr[12];
  snprintf(tsStr, sizeof(tsStr), "%lu", (unsigned long)getCurrentTimestamp());

  if (head + (int)strlen(tsStr) + (int)strlen(tail) + 1 > dstSize)
    return -1;

  memcpy(dst, src, head);
  dst[head] = '\0';
  strcat(dst, tsStr);
  strcat(dst, tail);
  return 0;
}

/* =====================================================================
 * @brief  JSON 解析 DTU 下发的指令
 *         外层提取 cominfo 字段，反转义后解析 {"cmd","value","deviceId"}
 * ===================================================================== */
static void parseDtuCommand(char *buf) {
  /* Step 1: 提取 cominfo 字段值（处理转义） */
  char *pCominfo = strstr(buf, "\"cominfo\":\"");
  if (!pCominfo) {
    printf("[DTU JSON] cominfo not found\r\n");
    return;
  }

  pCominfo += 11;
  char unescaped[200];
  int ui = 0;
  while (*pCominfo && ui < (int)sizeof(unescaped) - 1) {
    if (*pCominfo == '\\' && *(pCominfo + 1) != '\0') {
      char next = *(pCominfo + 1);
      if (next == '"' || next == '\\' || next == '/') {
        unescaped[ui++] = next;
        pCominfo += 2;
        continue;
      }
    } else if (*pCominfo == '"') {
      break;
    }
    unescaped[ui++] = *pCominfo++;
  }
  unescaped[ui] = '\0';
  printf("[DTU JSON] cominfo: %s\r\n", unescaped);

  /* Step 2: 提取各字段 */
  char cmd[32] = {0}, devId[32] = {0}, val[32] = {0};
  char *p;

  p = strstr(unescaped, "\"cmd\":");
  if (p) {
    p += 6;
    while (*p == ' ' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < 31)
      cmd[i++] = *p++;
    cmd[i] = '\0';
  }

  p = strstr(unescaped, "\"deviceId\":");
  if (p) {
    p += 11;
    while (*p == ' ' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < 31)
      devId[i++] = *p++;
    devId[i] = '\0';
  }

  p = strstr(unescaped, "\"value\":");
  if (p) {
    p += 8;
    while (*p == ' ' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < 31)
      val[i++] = *p++;
    val[i] = '\0';
    int vlen = (int)strlen(val);
    while (vlen > 0 && val[vlen - 1] == ',')
      val[--vlen] = '\0';
  }

  printf("[DTU JSON] cmd=%s, deviceId=%s, value=%s\r\n", cmd, devId, val);

  /* Step 3: 非本机指令 → 缓存 */
  if (strcmp(devId, DEVICE_NAME) != 0) {
    printf("[DTU CMD] Not this device (target=%s, local=%s), cache it\r\n",
           devId, DEVICE_NAME);
    cachePendingCommand(devId, cmd, val);
    return;
  }

  /* Step 4: 执行本机指令 */
  if (strcmp(cmd, "relayreboot") == 0) {
    printf("[DTU CMD] 执行设备重启...\r\n");
    Delay_Ms(500);
    NVIC_SystemReset();
  } else if (strcmp(cmd, "debug") == 0) {
    /* MID 板无 DTU 复位引脚，仅记录 */
    printf("[DTU CMD] debug: value=%s\r\n", val);
  } else if (strcmp(cmd, "A") == 0) {
    if (val[0] != '\0') {
      strncpy(configStr, val, sizeof(configStr) - 1);
      configStr[sizeof(configStr) - 1] = '\0';
      saveConfigToFlash(configStr);
      printf("[DTU CMD] Config updated: %s\r\n", configStr);
      Delay_Ms(1000);
      sendConfigToDtu();
    } else {
      printf("[DTU CMD] value 为空\r\n");
    }
  } else if (strcmp(cmd, "gpstm") == 0) {
    gpstmActive = 1;
    gpstmRecvTick = HAL_GetTick();
    gpstmTimeoutMin = (uint32_t)atoi(val);
    if (gpstmTimeoutMin == 0)
      gpstmTimeoutMin = 30;
    printf("[DTU CMD] gpstm 中继已记录: device=%s, timeout=%lu min\r\n",
           DEVICE_NAME, (unsigned long)gpstmTimeoutMin);
  } else if (strcmp(cmd, "txpower") == 0) {
    printf("[DTU CMD] txpower: target=%s, value=%s, cache it\r\n", devId, val);
    cachePendingCommand(devId, cmd, val);
    enqueueDtuMsg("config,get,csq", strlen("config,get,csq"));
    enqueueDtuMsg("config,get,nettime", strlen("config,get,nettime"));
    dtuCsqTick = HAL_GetTick();
    return;
  } else if (strcmp(cmd, "realyname") == 0) {
    char *eq = strchr(val, '=');
    if (eq != NULL) {
      *eq = '\0';
      printf("[DTU CMD] realyname: target=%s, newName=%s, cache it\r\n", val,
             eq + 1);
      cachePendingCommand(val, cmd, eq + 1);
    } else {
      printf("[DTU CMD] realyname: value 格式错误 (缺少 '=')\r\n");
    }
  } else {
    printf("[DTU CMD] 未知指令: %s\r\n", cmd);
  }
}

/* =====================================================================
 * @brief  串口中断收到 1 字节时调用
 *         开发/DTU 两种模式都解析（开发阶段可由串口助手模拟 DTU 下发指令）
 * ===================================================================== */
void dtu_uart_rx_byte(uint8_t b) {
  if (dtuRxLen < DTU_RX_BUF_SIZE) {
    dtuRxBuf[dtuRxLen++] = b;
    dtuLastRxTick = HAL_GetTick();
  }
}

/* =====================================================================
 * @brief  初始化：加载 Flash 配置，启动查询定时器
 * ===================================================================== */
void dtu_init(void) {
  loadConfigFromFlash();
  dtuCsqTick = HAL_GetTick();
}

/* =====================================================================
 * @brief  主循环轮询：队列发送 / 帧解析 / csq+nettime 周期查询
 *         开发模式同样运行（协议流带 [dtu] 前缀输出），仅自动重启限 DTU 模式
 * ===================================================================== */
void dtu_poll(void) {
  processDtuQueue();

  /* DTU 一帧完成判断：500ms 无新字节 或 缓冲区满 */
  if ((dtuRxLen > 0 && HAL_GetTick() - dtuLastRxTick > 500) ||
      dtuRxLen >= DTU_RX_BUF_SIZE) {
    if (dtuRxLen < DTU_RX_BUF_SIZE)
      dtuRxBuf[dtuRxLen] = '\0';
    else
      dtuRxBuf[DTU_RX_BUF_SIZE - 1] = '\0';

    char *pBuf = (char *)dtuRxBuf;

    /* 解析 CSQ: config,csq,ok,<value> */
    char *pCsq = strstr(pBuf, "config,csq,ok,");
    if (pCsq) {
      int csqVal = 0;
      if (sscanf(pCsq, "config,csq,ok,%d", &csqVal) == 1) {
        printf("[DTU] 信号强度: %d (%d%%)\r\n", csqVal, csqVal * 100 / 31);
        dtuCsqRssi = csqVal;
      }
    }

    /* 解析网络时间: config,nettime,ok,年,月,日,时,分,秒,毫秒 */
    char *pNet = strstr(pBuf, "config,nettime,ok,");
    if (pNet) {
      int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
      if (sscanf(pNet, "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", &y, &mo, &d,
                 &h, &mi, &s, &ms) >= 6) {
        uint32_t ts = dateTimeToTimestamp(y, mo, d, h, mi, s);
        printf("[DTU] 网络时间: %04d/%02d/%02d %02d:%02d:%02d ts=%lu\r\n", y,
               mo, d, h, mi, s, (unsigned long)ts);
        app_set_synced_time(ts); /* 写入主时间源 */
        dtuTimeValid = 1;
        dtuNetTimeFailCount = 0;

        sendTimeSyncToDtu(); /* 立即上报一次对时信息 */
        if (!configBootSent) { /* 开机首次上报配置 */
          configBootSent = 1;
          sendConfigToDtu();
        }
      }
    }

    /* 都没匹配到 → JSON 指令解析 */
    if (!pCsq && !pNet)
      parseDtuCommand(pBuf);

    dtuRxLen = 0;
  }

  /* 周期查询 csq+nettime：仅 DTU 正式模式主动查询；
   * 开发阶段不重试，由串口助手手动下发 nettime/csq 响应模拟 DTU */
#if (SERIAL_MODE == SERIAL_MODE_DTU)
  {
    uint32_t queryInterval = dtuTimeValid ? getConfigIntervalMs() : 5000;
    if (HAL_GetTick() - dtuCsqTick >= queryInterval) {
      dtuCsqTick = HAL_GetTick();
      dtu_wire_send((const uint8_t *)"config,get,csq\r\n", 16);
      Delay_Ms(300);
      dtu_wire_send((const uint8_t *)"config,get,nettime\r\n", 20);
      if (!dtuTimeValid) {
        dtuNetTimeFailCount++;
        printf("[DTU] Waiting for net time... (fail=%d/5)\r\n",
               dtuNetTimeFailCount);
        if (dtuNetTimeFailCount >= 5) {
          printf("[DTU] 连续 5 次获取网络时间失败，重启 STM32...\r\n");
          Delay_Ms(500);
          NVIC_SystemReset();
        }
      }
    }
  }
#endif
}
