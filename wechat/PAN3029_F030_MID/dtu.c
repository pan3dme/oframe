/* DTU 协议层（自 STM32_LORA_CENTEN 移植，适配 STM32F030 StdPeriph）
 * 与调试打印共用 USART1(PA9/PA10)：开发阶段协议流带 [dtu] 前缀，
 * 正式装机(DTU_PROD_MODE=1)原样收发且丢弃调试打印。
 * 功能：JSON 上报 / csq+nettime 周期查询 / JSON 指令解析 /
 *       待执行指令缓存 / gpstm 中继 / Flash 配置存储
 */
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 设备名，默认取 DEVICE_NAME_DEFAULT，启动时可能被 UID 表改写 */
char deviceName[16] = DEVICE_NAME_DEFAULT;

/* DTU 发送队列（环形缓冲，最多 5 条） */
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

/* DTU 接收缓冲 */
#define DTU_RX_BUF_SIZE 256
static uint8_t dtuRxBuf[DTU_RX_BUF_SIZE];
static uint16_t dtuRxLen = 0;
static uint32_t dtuLastRxTick = 0;

/* 查询 / 时间状态 */
static uint32_t dtuCsqTick = 0;         /* 周期查询定时器 */
static uint8_t dtuTimeValid = 0;        /* 网络时间是否已获取 */
static uint8_t dtuNetTimeFailCount = 0; /* 连续失败计数（5 次重启） */
static int dtuCsqRssi = 0;              /* CSQ 信号强度 */
static uint32_t rtcSendCount = 0;       /* 对时上报计数器 */
static int batteryValue = 100;          /* 电池电量（%） */
static uint8_t configBootSent = 0;      /* 开机配置是否已上报 */
static uint8_t configSendPending = 0;   /* 配置上报延迟待发 */
static uint32_t configSendTick = 0;
#define CONFIG_SEND_DELAY_MS 5000       /* 对时后延迟 5s 再报配置 */

/* Flash 配置（F030C8 最后一页 1KB，同一页分区存放，
 * 擦除以页为单位，任何一项写入都要整页重写）：
 *   +0  : CONFIG_MAGIC(4B) + 对时配置字符串(32B)
 *   +64 : CENTER_MAGIC(4B) + 农场中心坐标字符串(32B) */
#define CONFIG_FLASH_ADDR 0x0800FC00
#define CONFIG_MAGIC 0xA5A5A5A5
#define CENTER_FLASH_OFF 64
#define CENTER_MAGIC 0x5A5A5A5A
static char configStr[32] = "2,0M"; /* 对时上报间隔，如 "2,0M" */
static char centerStr[32] = "";     /* 农场中心坐标 "lat,lng"，空=未设置 */

/* 待执行指令缓存：每个设备最多一条 */
#define PENDING_CMD_MAX_SLOTS 8

typedef struct {
  uint8_t used;
  char deviceId[32];
  char cmd[32];
  char val[32];
} PendingCmd_t;

static PendingCmd_t pendingCmdCache[PENDING_CMD_MAX_SLOTS];

/* gpstm 中继状态 */
static uint8_t gpstmActive = 0;
static uint32_t gpstmRecvTick = 0;
static uint32_t gpstmTimeoutMin = 0;

/* LoRa 发送并等待完成（3s 超时保护） */
static void lora_tx_wait(void) {
  uint32_t waitStart = HAL_GetTick();
  while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE) {
    if (HAL_GetTick() - waitStart > 3000)
      break;
  }
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
}

/* 入队（队列满时丢弃最早一条） */
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

/* 协议流发送：开发阶段加 [dtu] 前缀区分调试打印 */
static void dtu_wire_send(const uint8_t *p, uint16_t len) {
#if !DTU_PROD_MODE
  Uart1_SendBytes((const uint8_t *)"[dtu] ", 6);
#endif
  Uart1_SendBytes(p, len);
}

/* 队列发送：每 1500ms 发一条，保证相邻两条间隔，给 DTU 回复时间 */
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

/* 统一上报（JSON 格式，入队） */
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

/* 对时上报：2|deviceName|timestamp|battery|count */
static void sendTimeSyncToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%lu|%d|%lu", MSG_TYPE_TIME,
           DEVICE_NAME, (unsigned long)getCurrentTimestamp(), batteryValue,
           (unsigned long)rtcSendCount++);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* 配置上报：6|deviceName|config_str|count */
static void sendConfigToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|%lu", MSG_TYPE_CONFIG,
           DEVICE_NAME, configStr, (unsigned long)rtcSendCount++);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* 农场中心坐标上报：1|deviceName|lat,lng|（未设置时报 "0,0" 保持 3 段） */
static void sendCenterToDtu(void) {
  char dataStr[64];
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|", MSG_TYPE_GPS, DEVICE_NAME,
           coord);
  printf("[DTU] 上报中心坐标: %s\r\n", dataStr);
  dtu_report_lora(dataStr, dtuCsqRssi, 0);
}

/* 从 Flash 读字符串 */
static void flash_read_str(uint32_t addr, char *dst, int dstSize) {
  const char *p = (const char *)addr;
  int i;
  for (i = 0; i < dstSize - 1 && p[i] != '\0'; i++)
    dst[i] = p[i];
  dst[i] = '\0';
}

/* 按半字写 Flash（F0 只支持半字编程），含结尾 '\0' */
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

/* 整页重写：配置 + 中心坐标必须一起写，否则擦页会把另一项擦掉 */
static void flash_save_all(void) {
  FLASH_Unlock();
  FLASH_ErasePage(CONFIG_FLASH_ADDR);

  FLASH_ProgramWord(CONFIG_FLASH_ADDR, CONFIG_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + 4, configStr);

  FLASH_ProgramWord(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF, CENTER_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF + 4, centerStr);

  FLASH_Lock();
}

/* 启动时加载配置 + 中心坐标 */
static void loadConfigFromFlash(void) {
  if (*(volatile uint32_t *)CONFIG_FLASH_ADDR == CONFIG_MAGIC) {
    flash_read_str(CONFIG_FLASH_ADDR + 4, configStr, sizeof(configStr));
    printf("[Flash] 加载配置: %s\r\n", configStr);
  } else {
    printf("[Flash] 无有效配置，使用默认: %s\r\n", configStr);
  }

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

/* configStr 第 1 段 = 对时间隔（分钟，1~60） */
static uint32_t getConfigIntervalMs(void) {
  int minutes = atoi(configStr);
  if (minutes < 1)
    minutes = 1;
  if (minutes > 60)
    minutes = 60;
  return (uint32_t)minutes * 60 * 1000;
}

/* 工作时段（62 进制 2 字符，与云端一致）
 * 字典顺序必须严格为 数字 + 大写 + 小写，
 * 已核对："00"=00:00~01:00, "0G"=00:00~17:00, "38"=10:00~20:00 */
#define TIME_DICT                                                              \
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

/* 2 字符 → idx（高位*62+低位）；不在字典返回 -1 */
static int twoCharToIndex(const char *s) {
  const char *p0 = strchr(TIME_DICT, s[0]);
  const char *p1 = strchr(TIME_DICT, s[1]);

  if (p0 == NULL || p1 == NULL)
    return -1;
  return (int)(p0 - TIME_DICT) * 62 + (int)(p1 - TIME_DICT);
}

/* 取 configStr 第一个逗号后的 2 字符 → idx；无字段/失败返回 -1 */
static int getWorkTimeFromConfig(void) {
  const char *comma = strchr(configStr, ',');
  char f[3];

  if (comma == NULL || comma[1] == '\0' || comma[2] == '\0')
    return -1;

  f[0] = comma[1];
  f[1] = comma[2];
  f[2] = '\0';
  return twoCharToIndex(f);
}

/* idx(0~275) → 小时窗口 [start,end)：起始 s 有 (23-s) 个窗口，累计 276 个 */
static uint8_t indexToTimeWindow(int idx, uint8_t *outStart, uint8_t *outEnd) {
  int sum = 0;

  if (idx < 0)
    return 0;

  for (int s = 0; s <= 22; s++) {
    int valid = 23 - s;

    if (idx < sum + valid) {
      *outStart = (uint8_t)s;
      *outEnd = (uint8_t)(s + 1 + (idx - sum));
      return 1;
    }
    sum += valid;
  }
  return 0;
}

/* end==23 视为 24，使 [0,23] 表示整天 */
static uint8_t workWindowEndAdj(uint8_t end) {
  return (end == 23) ? 24u : end;
}

/* 当前是否处于工作时段：1=是, 0=否。
 * 未对时 / 无时段字段 / idx 越界 一律返回 1（宁可耗电也不能睡死丢数据） */
uint8_t dtu_in_work_window(void) {
  uint8_t ws = 0, we = 0, hour, end;
  int wt;

  if (!dtuTimeValid)
    return 1;

  wt = getWorkTimeFromConfig();
  if (wt < 0)
    return 1;

  if (!indexToTimeWindow(wt, &ws, &we))
    return 1;

  hour = (uint8_t)((getCurrentTimestamp() % 86400UL) / 3600UL);
  end = workWindowEndAdj(we);

  return (hour >= ws && hour < end) ? 1 : 0;
}

/* 打印当前生效的工作时段（启动 / 收到新配置后调用） */
void dtu_print_work_window(const char *tag) {
  uint8_t ws = 0, we = 0, end;
  uint32_t hour = (getCurrentTimestamp() % 86400UL) / 3600UL;
  int wt = getWorkTimeFromConfig();

  if (wt < 0) {
    printf("%s配置无工作时段字段, 射频全天工作\r\n", tag);
    return;
  }
  if (!indexToTimeWindow(wt, &ws, &we)) {
    printf("%s工作时段 idx=%d 越界(0~275), 射频全天工作\r\n", tag, wt);
    return;
  }

  end = workWindowEndAdj(we);
  printf("%s%02u:00 ~ %02u:00, 当前 %02lu:00, 处于区间内=%d (1=是)\r\n", tag,
         ws, end, (unsigned long)hour, dtu_in_work_window());
}

/* 日期时间 → Unix 时间戳（秒） */
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

/* 缓存待执行指令（每设备一条，新指令替换旧指令） */
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

/* 指定设备是否有缓存指令：1=有, 0=无 */
uint8_t dtu_check_pending(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0)
      return 1;
  }
  return 0;
}

/* LoRa 回复缓存指令并清除：9|deviceId|cmd|val
 * 被动回复，由 OnSlave() 延时 LORA_REPLY_DELAY_CMD_MS 调用 */
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

/* gpstm 中继是否在有效期内 */
uint8_t dtu_gpstm_active(void) {
  if (!gpstmActive)
    return 0;
  return (HAL_GetTick() - gpstmRecvTick) < gpstmTimeoutMin * 60 * 1000 ? 1 : 0;
}

/* LoRa 回复 upgps：9|deviceId|upgps|0（被动回复，延时时长同上） */
void lora_reply_upgps(const char *deviceId) {
  char txData[64];
  int txLen =
      snprintf(txData, sizeof(txData), "%d|%s|upgps|0", MSG_TYPE_COM, deviceId);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa gpstm 中继] %s\r\n", txData);
  lora_tx_wait();
}

/* LoRa 回复对时：4|timestamp|deviceName
 * 被动回复，延时 LORA_REPLY_DELAY_TIMESYNC_MS(1500ms)，
 * 比指令类多 500ms，按子设备接收窗口实测值定 */
void lora_reply_time_sync(void) {
  char txData[64];
  int txLen = snprintf(txData, sizeof(txData), "%d|%lu|%s", MSG_TYPE_SYN_TIME,
                       (unsigned long)getCurrentTimestamp(), DEVICE_NAME);
  uint32_t tx_time = 0;
  rf_single_tx_data((uint8_t *)txData, (uint8_t)txLen, &tx_time);
  printf("[LoRa 下发对时] %s\r\n", txData);
  lora_tx_wait();
}

/* 替换上报数据第 3 段（时间字段）：
 * >=10 位数字视为完整时间戳原样保留；否则视为"当天 00:00 起的秒数"，
 * 用本机当天 00:00 补齐。0=成功, -1=格式不符或缓冲区不足 */
int dtu_replace_time_segment(const char *src, char *dst, int dstSize) {
  const char *p1 = strchr(src, '|');
  if (!p1)
    return -1;
  const char *p2 = strchr(p1 + 1, '|');
  if (!p2)
    return -1;

  const char *pSeg = p2 + 1;   /* 时间字段起点 */
  const char *p3 = strchr(pSeg, '|');
  const char *tail = p3 ? p3 : (src + strlen(src));
  int head = (int)(pSeg - src); /* "<type>|<deviceName>|" */

  int segLen = (int)(tail - pSeg);
  char segBuf[16];
  if (segLen <= 0 || segLen >= (int)sizeof(segBuf))
    return -1;
  memcpy(segBuf, pSeg, segLen);
  segBuf[segLen] = '\0';

  uint32_t recvVal = (uint32_t)strtoul(segBuf, NULL, 10);
  uint32_t reportTs;
  if (recvVal >= 1000000000UL) {
    reportTs = recvVal;                     /* 已是完整时间戳 */
  } else {
    uint32_t now = getCurrentTimestamp();
    uint32_t todayMidnight = now - (now % 86400UL); /* 本机当天 00:00 */
    reportTs = todayMidnight + recvVal;
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

/* 还原压缩坐标（第 3 段无小数点 = 压缩格式，单位 1e-5 度、相对农场中心）：
 * "0,12" → 中心 + (0.00000, 0.00012)，一帧省 11~14 字节
 * 0=已还原, -1=无需还原(完整坐标/格式不符), -2=未设中心坐标 */
int dtu_expand_coord(const char *src, char *dst, int dstSize) {
  const char *p1 = strchr(src, '|');
  if (!p1)
    return -1;
  const char *p2 = strchr(p1 + 1, '|');
  if (!p2)
    return -1;

  const char *pSeg = p2 + 1; /* 坐标字段起点 */
  const char *p3 = strchr(pSeg, '|');
  const char *tail = p3 ? p3 : (src + strlen(src));
  int segLen = (int)(tail - pSeg);
  if (segLen <= 0 || segLen >= 24)
    return -1;

  /* 有小数点 = 完整坐标（含 "0.0,0.0" 失败值），原样上报 */
  if (memchr(pSeg, '.', (size_t)segLen) != NULL)
    return -1;

  char segBuf[24];
  memcpy(segBuf, pSeg, (size_t)segLen);
  segBuf[segLen] = '\0';

  long latOff = 0, lngOff = 0;
  if (sscanf(segBuf, "%ld,%ld", &latOff, &lngOff) != 2)
    return -1;

  /* 基准点：mapcenter 保存的农场中心坐标 */
  if (centerStr[0] == '\0')
    return -2;

  double clat = 0.0, clng = 0.0;
  if (sscanf(centerStr, "%lf,%lf", &clat, &clng) != 2)
    return -2;

  char coord[24];
  snprintf(coord, sizeof(coord), "%.5f,%.5f",
           clat + (double)latOff / 100000.0,
           clng + (double)lngOff / 100000.0);

  int head = (int)(pSeg - src);
  if (head + (int)strlen(coord) + (int)strlen(tail) + 1 > dstSize)
    return -1;

  memcpy(dst, src, (size_t)head);
  dst[head] = '\0';
  strcat(dst, coord);
  strcat(dst, tail);
  return 0;
}

/* 经纬度合法性校验：1=合法, 0=非法 */
static int center_coord_valid(double lat, double lng) {
  if (lat < -90.0 || lat > 90.0)
    return 0;
  if (lng < -180.0 || lng > 180.0)
    return 0;
  return 1;
}

/* 解析 DTU 下发指令：外层取 cominfo，反转义后解析 cmd/value/deviceId */
static void parseDtuCommand(char *buf) {
  /* Step 1: 提取 cominfo（处理转义） */
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
    /* 设置农场中心坐标：校验格式和范围，合法才写内存 + Flash。
     * 用 double 解析，避免 8 位有效数字被 float 截断 */
    double la = 0.0, lo = 0.0;

    if (sscanf(val, "%lf,%lf", &la, &lo) == 2 && center_coord_valid(la, lo)) {
      /* 归一化成 5 位小数：帧长固定，也不含会破坏 JSON 的字符 */
      snprintf(centerStr, sizeof(centerStr), "%.5f,%.5f", la, lo);
      flash_save_all();
      printf("[DTU CMD] 中心坐标已永久保存: %s\r\n", centerStr);
    } else {
      printf("[DTU CMD] mapcenter: 坐标非法已拒绝(原值=%s 保持): %s\r\n",
             centerStr, val);
    }
    /* 无论是否合法都回报当前内存中的坐标 */
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
      /* 工作时段实时解析，改完立即生效 */
      dtu_print_work_window("[DTU CMD] 新时段: ");
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
  } else if (strcmp(cmd, "lorasw") == 0) {
    /* 远程开关射频：1=开启（时段外拒绝，时段优先），0=休眠（任何时候允许）。
     * 休眠只关射频，MCU/DTU 照常工作，仍可远程唤醒；状态只存 RAM */
    if (atoi(val) != 0) {
      if (!dtu_in_work_window()) {
        printf("[DTU CMD] lorasw: 当前不在工作时段内, 拒绝开启射频\r\n");
      } else if (app_lora_is_on()) {
        printf("[DTU CMD] lorasw: 当前已是收发状态, 无需重复开启\r\n");
      } else {
        app_lora_switch(1);
      }
    } else {
      if (!app_lora_is_on()) {
        printf("[DTU CMD] lorasw: 当前已是休眠状态, 无需重复关闭\r\n");
      } else {
        app_lora_switch(0);
      }
    }
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

/* 串口中断收到 1 字节时调用 */
void dtu_uart_rx_byte(uint8_t b) {
  if (dtuRxLen < DTU_RX_BUF_SIZE) {
    dtuRxBuf[dtuRxLen++] = b;
    dtuLastRxTick = HAL_GetTick();
  }
}

/* 初始化：加载 Flash 配置，启动查询定时器 */
void dtu_init(void) {
  loadConfigFromFlash();
  dtuCsqTick = HAL_GetTick();
}

/* 主循环轮询：队列发送 / 帧解析 / csq+nettime 周期查询 */
void dtu_poll(void) {
  processDtuQueue();

  /* 开机首次配置上报：对时上报后延迟 5s 入队 */
  if (configSendPending &&
      HAL_GetTick() - configSendTick >= CONFIG_SEND_DELAY_MS) {
    configSendPending = 0;
    sendConfigToDtu();
  }

  /* 一帧结束：500ms 无新字节 或 缓冲区满 */
  if ((dtuRxLen > 0 && HAL_GetTick() - dtuLastRxTick > 500) ||
      dtuRxLen >= DTU_RX_BUF_SIZE) {
    if (dtuRxLen < DTU_RX_BUF_SIZE)
      dtuRxBuf[dtuRxLen] = '\0';
    else
      dtuRxBuf[DTU_RX_BUF_SIZE - 1] = '\0';

    char *pBuf = (char *)dtuRxBuf;

    /* CSQ: config,csq,ok,<value> */
    char *pCsq = strstr(pBuf, "config,csq,ok,");
    if (pCsq) {
      int csqVal = 0;
      if (sscanf(pCsq, "config,csq,ok,%d", &csqVal) == 1) {
        printf("[DTU] 信号强度: %d (%d%%)\r\n", csqVal, csqVal * 100 / 31);
        dtuCsqRssi = csqVal;
      }
    }

    /* 网络时间: config,nettime,ok,年,月,日,时,分,秒,毫秒 */
    char *pNet = strstr(pBuf, "config,nettime,ok,");
    if (pNet) {
      int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
      if (sscanf(pNet, "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", &y, &mo, &d,
                 &h, &mi, &s, &ms) >= 6) {
        uint32_t ts = dateTimeToTimestamp(y, mo, d, h, mi, s);
        printf("[DTU] 网络时间: %04d/%02d/%02d %02d:%02d:%02d ts=%lu\r\n", y,
               mo, d, h, mi, s, (unsigned long)ts);
        app_set_synced_time(ts);
        dtuTimeValid = 1;
        dtuNetTimeFailCount = 0;

        sendTimeSyncToDtu();
        if (!configBootSent) {
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

  /* 周期查询 csq+nettime：统一入队，相邻 1.5s 一条。
   * 对时后间隔由 configStr 控制，未对时固定 5s 重试；
   * 连续失败 5 次重启仅正式装机模式启用（桌面调试 DTU 口可能空接） */
  {
    uint32_t queryInterval = dtuTimeValid ? getConfigIntervalMs() : 5000;
    if (HAL_GetTick() - dtuCsqTick >= queryInterval) {
      dtuCsqTick = HAL_GetTick();
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
