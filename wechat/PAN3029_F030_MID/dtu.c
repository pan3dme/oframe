/**********************************************************************************************
 * @file dtu.c
 * @brief DTU 协议层（自 STM32_LORA_CENTEN 移植，适配 STM32F030 StdPeriph 库）
 *        - 与调试打印共用 USART1(PA9/PA10)：开发阶段协议流带 [dtu] 前缀，
 *          正式装机(DTU_PROD_MODE=1)原样收发且丢弃调试打印
 *        - 功能：JSON 上报 / csq+nettime 周期查询 / JSON 指令解析 /
 *          待执行指令缓存 / gpstm 中继 / Flash 配置存储
 ********************************************************************************************/
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================== DTU 发送队列：环形缓冲区
 * ========================== */
#define DTU_QUEUE_SIZE 5
#define DTU_MSG_MAX_LEN 256

typedef struct {
  char buf[DTU_MSG_MAX_LEN];
  uint16_t len;
} DtuQueueItem_t;

static DtuQueueItem_t dtuQueue[DTU_QUEUE_SIZE];
static uint8_t dtuQueueHead = 0;
static uint8_t dtuQueueTail = 0;
static uint8_t dtuQueueCount = 0;
static uint32_t dtuLastSendTick = 0;

/* ========================== DTU 接收缓冲区 ========================== */
#define DTU_RX_BUF_SIZE 256
static uint8_t dtuRxBuf[DTU_RX_BUF_SIZE];
static uint16_t dtuRxLen = 0;
static uint32_t dtuLastRxTick = 0;

/* ========================== 查询 / 时间状态 ========================== */
static uint32_t dtuCsqTick = 0;         /* 周期查询定时器 */
static uint8_t dtuTimeValid = 0;        /* DTU 网络时间是否已获取 */
static uint8_t dtuNetTimeFailCount = 0; /* 连续失败计数（5 次重启） */
static int dtuCsqRssi = 0;              /* DTU CSQ 信号强度 */
static uint32_t rtcSendCount = 0;       /* 对时上报计数器 */
static int batteryValue = 100;          /* 电池电量（%） */
static uint8_t configBootSent = 0;      /* 开机配置是否已上报 */
static uint8_t configSendPending = 0;   /* 配置上报延迟待发标志 */
static uint32_t configSendTick = 0;     /* 配置上报延迟计时起点 */
#define CONFIG_SEND_DELAY_MS 5000       /* 对时上报后延迟 5s 再上报配置 */

/* ========================== Flash 配置存储（F030C8 最后一页 1KB）
 * ==========================
 * 同一页内分区存放（擦除以页为单位，所以任何一项写入都要整页重写）：
 *   +0  : CONFIG_MAGIC(4B) + 对时配置字符串(32B)
 *   +64 : CENTER_MAGIC(4B) + 农场中心坐标字符串(32B)
 * ===================================================================== */
#define CONFIG_FLASH_ADDR 0x0800FC00
#define CONFIG_MAGIC 0xA5A5A5A5
#define CENTER_FLASH_OFF 64 /* 中心坐标区在配置页内的偏移 */
#define CENTER_MAGIC 0x5A5A5A5A
static char configStr[32] = "2,0M"; /* 对时上报间隔，如 "2,0M" */
static char centerStr[32] = "";     /* 农场中心坐标 "lat,lng"，空=未设置 */

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
static uint8_t gpstmActive = 0;
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
 *         统一存储字符串：原始命令和 JSON 上报都走同一个队列
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
 *         正式装机原样发送
 * ===================================================================== */
static void dtu_wire_send(const uint8_t *p, uint16_t len) {
#if !DTU_PROD_MODE
  Uart1_SendBytes((const uint8_t *)"[dtu] ", 6);
#endif
  Uart1_SendBytes(p, len);
}

/* =====================================================================
 * @brief  处理 DTU 发送队列：每 1500ms 从队头取出一条发送
 *         所有向 DTU 的发送统一走本队列（JSON 上报 / csq+nettime 查询 /
 *         开机配置上报），保证相邻两条至少间隔 1.5s，DTU 有时间回复
 * ===================================================================== */
static void processDtuQueue(void) {
  if (dtuQueueCount == 0)
    return;

  if (HAL_GetTick() - dtuLastSendTick >= 1500) {
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
                     "\n{"
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
                     loraStr, DEVICE_NAME, (unsigned long)getCurrentTimestamp(),
                     rssi, snr);

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
 * @brief  上报农场中心坐标：1|deviceName|lat,lng|
 *         （MSG_TYPE_GPS = 1，与终端 GPS 上报类型一致）
 *         未设置过坐标时第 3 段上报 "0,0"，保证帧结构固定为 3 段，
 *         云端/小程序解析不会拿到空串
 * ===================================================================== */
static void sendCenterToDtu(void) {
  char dataStr[64];
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|", MSG_TYPE_GPS, DEVICE_NAME,
           coord);
  printf("[DTU] 上报中心坐标: %s\r\n", dataStr);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* =====================================================================
 * @brief  从 Flash 读出一个以 '\0' 结尾的字符串
 * ===================================================================== */
static void flash_read_str(uint32_t addr, char *dst, int dstSize) {
  const char *p = (const char *)addr;
  int i;
  for (i = 0; i < dstSize - 1 && p[i] != '\0'; i++)
    dst[i] = p[i];
  dst[i] = '\0';
}

/* =====================================================================
 * @brief  把一个字符串（含结尾 '\0'）按半字写入 Flash
 *         STM32F0 只能半字编程；用 i+1<=len 兜底，
 *         避免读到源串 '\0' 之后的越界字节
 * ===================================================================== */
static void flash_write_str(uint32_t addr, const char *str) {
  int len = (int)strlen(str);
  int i;
  for (i = 0; i <= len; i += 2) {
    uint16_t halfWord = (uint16_t)str[i];
    if (i + 1 <= len)
      halfWord |= ((uint16_t)str[i + 1]) << 8;
    FLASH_ProgramHalfWord(addr + i, halfWord);
  }
}

/* =====================================================================
 * @brief  整页重写：对时配置 + 农场中心坐标
 *         必须两项一起写：Flash 擦除以页为单位，
 *         只写其中一项会把同页里的另一项一起擦掉
 * ===================================================================== */
static void flash_save_all(void) {
  FLASH_Unlock();
  FLASH_ErasePage(CONFIG_FLASH_ADDR);

  FLASH_ProgramWord(CONFIG_FLASH_ADDR, CONFIG_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + 4, configStr);

  FLASH_ProgramWord(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF, CENTER_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF + 4, centerStr);

  FLASH_Lock();
}

/* =====================================================================
 * @brief  启动时从 Flash 加载对时配置 + 农场中心坐标
 * ===================================================================== */
static void loadConfigFromFlash(void) {
  /* 对时配置 */
  if (*(volatile uint32_t *)CONFIG_FLASH_ADDR == CONFIG_MAGIC) {
    flash_read_str(CONFIG_FLASH_ADDR + 4, configStr, sizeof(configStr));
    printf("[Flash] 加载配置: %s\r\n", configStr);
  } else {
    printf("[Flash] 无有效配置，使用默认: %s\r\n", configStr);
  }

  /* 农场中心坐标 */
  if (*(volatile uint32_t *)(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF) ==
      CENTER_MAGIC) {
    flash_read_str(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF + 4, centerStr,
                   sizeof(centerStr));
    printf("[Flash] 加载中心坐标: %s\r\n", centerStr);
  } else {
    centerStr[0] = '\0';
    printf("[Flash] 无中心坐标（未设置）\r\n");
  }
}

/* =====================================================================
 * @brief  从 configStr 解析对时间隔（毫秒），限制 1~60 分钟
 *         configStr 格式: "<分钟>,<子参数>M"，例如 "2,0M" = 2 分钟
 *         atoi() 只取第一个数字，后面的 ",0M" 忽略
 * ===================================================================== */
static uint32_t getConfigIntervalMs(void) {
  int minutes = atoi(configStr);
  if (minutes < 1)
    minutes = 1;
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
 * @brief  通过 LoRa 回复缓存指令给目标设备并清除缓存
 *         格式: 9|deviceId|cmd|val
 *         由 main.c OnSlave() 在收到该设备上报后，延时
 *         LORA_REPLY_DELAY_CMD_MS(1000ms) 调用，落在子设备接收窗口内
 * ===================================================================== */
uint8_t lora_reply_pending_cmd(const char *deviceId) {
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
 * @brief  LoRa 回复 upgps 指令：9|deviceId|upgps|0
 *         由 main.c OnSlave() 在收到该设备上报后，延时
 *         LORA_REPLY_DELAY_CMD_MS(1000ms) 调用，落在子设备接收窗口内
 * ===================================================================== */
void lora_reply_upgps(const char *deviceId) {
  char txData[64];
  int txLen =
      snprintf(txData, sizeof(txData), "%d|%s|upgps|0", MSG_TYPE_COM, deviceId);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa gpstm 中继] %s\r\n", txData);
  lora_tx_wait();
}

/* =====================================================================
 * @brief  LoRa 回复对时消息：4|timestamp|deviceName
 *         被动回复而非主动下发：只在收到子设备上报后，由 main.c OnSlave()
 *         延时 LORA_REPLY_DELAY_TIMESYNC_MS(1500ms) 调用。
 *         延时的原因：子设备发完 LoRa 需要从 TX 切回 RX，1.5s 后本中继
 *         才下发，正好落在子设备的接收窗口内，否则子设备收不到。
 *         对时比指令类回复（1000ms）多等 500ms，是按子设备侧实测窗口定的。
 * ===================================================================== */
void lora_reply_time_sync(void) {
  char txData[64];
  int txLen = snprintf(txData, sizeof(txData), "%d|%lu|%s", MSG_TYPE_SYN_TIME,
                       (unsigned long)getCurrentTimestamp(), DEVICE_NAME);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa 下发对时] %s\r\n", txData);
  lora_tx_wait();
}

/* =====================================================================
 * @brief  加工并替换上报数据中的时间字段（第 3 个 '|' 分隔段）
 *         规则：
 *         - 收到的时间字段为含日期的完整时间戳（>=10 位数字）时，原样保留；
 *         - 否则视为“当天从 00:00 起的秒数”，用本机当天 00:00 时间戳补全。
 *         本机日期来源：getCurrentTimestamp()（网络对时后为真实时间）。
 * @retval 0=成功, -1=格式不符或缓冲区不足
 * ===================================================================== */
int dtu_replace_time_segment(const char *src, char *dst, int dstSize) {
  const char *p1 = strchr(src, '|');
  if (!p1)
    return -1;
  const char *p2 = strchr(p1 + 1, '|');
  if (!p2)
    return -1;

  const char *pSeg = p2 + 1;   /* 时间字段起点（第二个 '|' 之后） */
  const char *p3 = strchr(pSeg, '|');
  const char *tail = p3 ? p3 : (src + strlen(src)); /* 从第 3 个 '|' 起的尾部 */
  int head = (int)(pSeg - src); /* 含第二个 '|'：即 "<type>|<deviceName>|" */

  /* 取出原始时间字段（原地无 '\0'），拷贝到本地解析 */
  int segLen = (int)(tail - pSeg);
  char segBuf[16];
  if (segLen <= 0 || segLen >= (int)sizeof(segBuf))
    return -1;
  memcpy(segBuf, pSeg, segLen);
  segBuf[segLen] = '\0';

  /* 判断：>=10 位数字视为含日期的完整时间戳，原样使用；
   * 否则视为“当天从 00:00 起的秒数”，用本机日期补全成完整时间戳 */
  uint32_t recvVal = (uint32_t)strtoul(segBuf, NULL, 10);
  uint32_t reportTs;
  if (recvVal >= 1000000000UL) {
    reportTs = recvVal;                     /* 已是完整 Unix 时间戳 */
  } else {
    uint32_t now = getCurrentTimestamp();   /* 本机当前时间 */
    uint32_t todayMidnight = now - (now % 86400UL); /* 本机当天 00:00 */
    reportTs = todayMidnight + recvVal;     /* 补全为当天真实时间戳 */
  }

  char tsStr[12];
  snprintf(tsStr, sizeof(tsStr), "%lu", (unsigned long)reportTs);

  if (head + (int)strlen(tsStr) + (int)strlen(tail) + 1 > dstSize)
    return -1;

  memcpy(dst, src, head);
  dst[head] = '\0';
  strcat(dst, tsStr);
  strcat(dst, tail);
  return 0;
}

/* =====================================================================
 * @brief  校验经纬度是否在合法地理范围内
 * @retval 1=合法, 0=非法
 * ===================================================================== */
static int center_coord_valid(double lat, double lng) {
  if (lat < -90.0 || lat > 90.0)
    return 0;
  if (lng < -180.0 || lng > 180.0)
    return 0;
  return 1;
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
  } else if (strcmp(cmd, "mapcenter") == 0) {
    /* 设置农场中心坐标：先验格式和地理范围，只有合法才写内存 + Flash。
     * 用 double 解析，避免 109.39087 这类 8 位有效数字被 float 截断。 */
    double la = 0.0, lo = 0.0;

    if (sscanf(val, "%lf,%lf", &la, &lo) == 2 && center_coord_valid(la, lo)) {
      /* 归一化成 5 位小数存储：帧长固定，且天然不含 " \ | 等会破坏
       * JSON 拼接的字符 */
      snprintf(centerStr, sizeof(centerStr), "%.5f,%.5f", la, lo);
      flash_save_all();
      printf("[DTU CMD] 中心坐标已永久保存: %s\r\n", centerStr);
    } else {
      /* 非法：不写内存、不写 Flash，保持上一次的合法值 */
      printf("[DTU CMD] mapcenter: 坐标非法已拒绝(原值=%s 保持): %s\r\n",
             centerStr, val);
    }
    /* 无论本次是否合法，都上报当前内存中的坐标：
     * 合法  -> 回新坐标；非法 -> 回上一个合法值（未设过则 "0,0"）
     * 格式: 1|deviceName|lat,lng| */
    sendCenterToDtu();
  } else if (strcmp(cmd, "debug") == 0) {
    /* MID 板无 DTU 复位引脚，仅记录 */
    printf("[DTU CMD] debug: value=%s\r\n", val);
  } else if (strcmp(cmd, "A") == 0) {
    if (val[0] != '\0') {
      strncpy(configStr, val, sizeof(configStr) - 1);
      configStr[sizeof(configStr) - 1] = '\0';
      flash_save_all();
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
 *         自动重启仅正式装机模式(DTU_PROD_MODE=1)启用
 * ===================================================================== */
void dtu_poll(void) {
  processDtuQueue();

  /* 开机首次配置上报：对时上报后延迟 5s 再入队，保证两条不挤在一起 */
  if (configSendPending &&
      HAL_GetTick() - configSendTick >= CONFIG_SEND_DELAY_MS) {
    configSendPending = 0;
    sendConfigToDtu();
  }

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

        /* 每次拿到网络时间后上报对时信息（周期由 configStr 控制，如
         * "2,0M"=2分钟） 流程：定时查询 csq+nettime → DTU 回复 nettime →
         * 上报对时 JSON */
        sendTimeSyncToDtu();
        if (!configBootSent) {
          /* 开机首次：对时上报后延迟 5s 再上报配置，避免两条挤在一起 */
          configBootSent = 1;
          configSendPending = 1;
          configSendTick = HAL_GetTick();
        }
      }
    }

    /* 都没匹配到 → JSON 指令解析 */
    if (!pCsq && !pNet)
      parseDtuCommand(pBuf);

    dtuRxLen = 0;
  }

  /* 周期查询 csq+nettime（始终主动查询）：统一入队，相邻 1.5s 发一条；
   * 对时成功后间隔由 configStr 控制（如 "2,0M"=2分钟），未对时时固定 5s 重试；
   * 对时失败 5 次自动重启仅正式装机模式(DTU_PROD_MODE=1)启用，
   * 桌面调试时 DTU 口可能空接，不重启避免反复复位 */
  {
    uint32_t queryInterval = dtuTimeValid ? getConfigIntervalMs() : 5000;
    if (HAL_GetTick() - dtuCsqTick >= queryInterval) {
      dtuCsqTick = HAL_GetTick();
      /* 两条查询入队，processDtuQueue 会以 1.5s 间隔逐条发出（末尾 \r\n
       * 由队列统一追加）*/
      enqueueDtuMsg("config,get,csq", strlen("config,get,csq"));
      enqueueDtuMsg("config,get,nettime", strlen("config,get,nettime"));
      if (!dtuTimeValid) {
        dtuNetTimeFailCount++;
        printf("[DTU] Waiting for net time... (fail=%d/5)\r\n",
               dtuNetTimeFailCount);
        if (DTU_PROD_MODE && dtuNetTimeFailCount >= 5) {
          printf("[DTU] 连续 5 次获取网络时间失败，重启 STM32...\r\n");
          Delay_Ms(500);
          NVIC_SystemReset();
        }
      }
    }
  }
}
