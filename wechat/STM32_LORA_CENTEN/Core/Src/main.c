/* USER CODE BEGIN Header */
/**
 * CENTEN LoRa/4G gateway, STM32F103C8T6 + PAN3029.
 * UART IRQs only receive bytes; RF IRQs only record events.
 * Packet parsing, RF operations and downlink scheduling run in main.
 * See README_CENTEN.md for protocol limits and validation.
 * Copyright (c) 2025 STMicroelectronics. All rights reserved.
 * Licensed under LICENSE when supplied; otherwise provided AS-IS.
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
// STM32 HAL 库主头文件，包含所�?HAL 驱动接口
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "centen_support.h"
#include "centen_uart.h"
  #include "common_test.h"  /* device-name/msg-type helpers (project-local Common/) */
#include "pan3029_port.h" // PAN3029 硬件端口适配层头文件
#include "pan3029_rf.h"   // PAN3029 LoRa 射频驱动头文�?
#include "radio.h"        // LoRa 射频参数配置 (DEFAULT_FREQ/DEFAULT_SF �?
#include "stdio.h"        // 标准输入输出库，用于 printf 调试输出
#include "stdlib.h"       // 标准库函数，提供 atoi() 等工具函�?
#include "string.h"       // 字符串处理库，提�?memcpy/strncmp/memset 等函�?

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* DTU 发送队列：环形缓冲区，每条消息最�?256 字节，最�?5 �?*/
#define DTU_QUEUE_SIZE 5
#define DTU_MSG_MAX_LEN 384

/* --- DTU platform PING heartbeat (server->4G->DTU->uart end-to-end link
 * proof). Platform polls e.g. "PING1200"; the number in the frame IS the
 * timeout window in seconds. Lost heartbeat -> link broken -> RST DTU,
 * then report "8|dev|DTURST|PING<win>| once the link recovers */
static uint32_t dtuPingWinMs      = 1200000; /* default 1200s until 1st PING */
static uint32_t dtuLastPingTick   = 0;       /* HAL_GetTick() of last PING */
static uint8_t  dtuPingGot        = 0;       /* 1 = at least one PING seen */
static uint32_t dtuPingGraceStart = 0;       /* grace start (boot / post-RST) */
static uint8_t  dtuRstByPing      = 0;       /* 1 = RST'd by PING timeout */

typedef struct {
  char buf[DTU_MSG_MAX_LEN]; // 消息内容
  uint16_t len;              // 消息长度
} DtuQueueItem_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "CENTEN LoRa gateway" // 工程名称
#define DEMO_VER "V1.1.2"                // 演示版本�?
#define EXAMPLE_DATE "2026/11/24"      // 工程日期

#define RX_LEN 96             // LoRa 接收缓冲区最大长度（字节�?
#define CENTEN_LORA_TX_MAX 36 /* max bytes sent in one LoRa frame (truncated) */
/* LoRa 接收后延时下发时间（ms）：指令�?upgps �?1000，对时帧�?1500 */
#define LORA_CMD_DELAY_MS      800  // 有缓存指令或 gpstm 有效时，收到上行后延时下发指�?
#define LORA_SYNCTIME_DELAY_MS 1200 // 无指令时，收到上行后延时回对时帧

/* Work mode - 工作模式定义 */
#define MODE_IDLE 0 // 空闲模式
#define MODE_RX 1   // 接收模式
/* MODE_TX 已删除：进入该模式的唯一途径是按�?K2，K2 移除后无触发源，
 * 连同 do_tx_send() / txMsg / lastTxTick 一并移除�?
 * 恢复定时发射时把 #define MODE_TX 2 补回即可 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* hcan(CAN) / hi2c1(I2C) 已删除：
 * 两者的 MX_xxx_Init() 在整个工程里从未定义也从未调用，
 * �?CAN / I2C 外设从未被初始化过，属于死代码，
 * 连同 MSP 初始化函数、CAN 的两�?ISR、HAL 驱动�?.ioc 引脚一并移�?*/
SPI_HandleTypeDef hspi1; // SPI1 用于 LoRa PAN3029（硬�?SPI�?
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3; // USART3 用于 DTU 通信

static char deviceName[16] = "v0-100"; /* default; bindDeviceId renames+persists */
 
/* USER CODE BEGIN PV */

// PB15 (LED1) 接收指示：收到LoRa数据时亮500ms
static uint32_t led1OnTick = 0; // LED1 点亮时间�?
static uint8_t led1On = 0;      // LED1 当前是否点亮�?=�? 1=亮）

// 工作模式相关

static uint8_t workMode = MODE_RX;     // 当前工作模式（RX/TX/IDLE�?
                                       // （txMsg/lastTxTick 已随 do_tx_send 移除�?
                                       //   原按键消抖时间戳已随 K1/K2 移除�?

/* LoRa receive buffers - LoRa 接收缓冲�?*/

extern struct RxDoneMsg
    RxDoneParams; // 外部声明：接收完成时的消息结构体（在驱动中定义）

/* DTU (USART3) 接收缓冲�?*/
static CentenFramer dtuFrame;
static uint32_t dtuOverflowSeen;
static uint32_t dtuRecoveryStart;
static uint32_t dtuQueryTick;
static uint8_t dtuQueryStage;
static uint8_t dtuQueryPending;
static uint8_t dtuQueryAnswered;
static uint32_t dtuResetCount;
static uint32_t dtuParseRejected;
static uint32_t dtuTxFailures;
static uint32_t dtuQueueDropped;
static uint8_t syncReportPending;
static uint32_t configActiveAddr;
static uint32_t configSequence;

/* ISR only records events. All PAN3029/SPI operations run in main context. */
static volatile uint8_t radioIrqPending;
static uint8_t radioTxActive;
static uint8_t radioPowerDirty;
static uint32_t radioTxStart;
static int radioTxPendingSlot = -1;
static uint32_t radioTxGeneration;
static uint32_t pendingGeneration;
static uint8_t radioRecoverPending;
static uint32_t radioRecoverTick;
static uint32_t radioTxTimeouts;
static uint32_t radioRxRejected;
static uint32_t radioJobsDropped;
#define DTU_RECOVERY_GRACE_MS 180000u
#define DTU_QUERY_WAIT_MS 5000u
#define CONFIG_PAGE_A 0x0800F800u
#define CONFIG_PAGE_B 0x0800FC00u
#define LORA_JOB_COUNT 8
typedef struct {
  uint8_t used, kind;
  char deviceId[32];
  uint32_t due;
} LoraJob;
static LoraJob loraJobs[LORA_JOB_COUNT];
extern int centen_radio_spi_failed(void);
extern void centen_radio_spi_clear(void);

static uint32_t dtuCsqTick = 0;      // nettime 查询定时器：记录上次发送时�?
static char     dtuIccidStr[24] = ""; // SIM �?ICCID�?0 位，续费/换卡核对用）
static uint8_t  dtuIccidGot = 0;     // 1=已获取到 ICCID，不再重复查�?
static char     dtuImsiStr[24] = ""; // SIM �?IMSI�?5 位，用户识别码）
static uint8_t  dtuImsiGot = 0;      // 1=已获取到 IMSI，不再重复查�?
static uint32_t dtuIccidTick = 0;    // ICCID/IMSI 查询独立定时器（拿到网络时间后才启动�?
static uint32_t dtuCsqAnswerTick = 0; // 最近一次收�?CSQ 应答的时刻（0=还没收到过）
static uint8_t  dtuRstByCsq = 0;     // 1=�?CSQ 死机复位�?DTU，恢复联网后上报一次告知服务器
static uint32_t syncReportDue = 0;   // 上报到期时刻（nettime 成功后延�?10 秒，0=无待报）
static uint32_t dtuTimestamp = 0;    // DTU 网络时间 Unix 时间戳（秒）
static uint32_t dtuSyncTick = 0;     // 对时时的 HAL_GetTick() 值，用于实时计算
static uint8_t dtuTimeValid = 0;     // 网络时间是否已获取成�?
static int dtuCsqRssi = 0;           // DTU CSQ 信号强度�?
static uint8_t dtuNetTimeFailCount = 0;  // DTU 网络时间连续失败计数器（达到 3 次复�?DTU�?
static uint32_t rtcSendCount = 0;    // 对时上报计数�?
static uint8_t localTxPower = DEFAULT_PWR; // 中继当前发射功率（txpower 指令修改，不落盘，重启恢复默认）
static int batteryValue = 85;        // 电池电量�?�?
static char configStr[32] = "30,0M"; // 配置信息字符串（可通过DTU修改�?
static char centerStr[32] = "";      // 农场中心坐标 "lat,lng"（空=未设置）
/* 中心坐标数值缓存（1e-5 度）：还原子设备"相对偏移"上报用�?
 * �?centerStr �?加载Flash / mapcenter 设置 两处同步 */
static int32_t relayCenterLat = 0;
static int32_t relayCenterLng = 0;
static uint8_t relayCenterValid = 0;
static uint8_t configBootSent = 0;   // 开机配置是否已上报�?=未上�? 1=已上报）

/* DTU 发送队列变�?*/
static DtuQueueItem_t dtuQueue[DTU_QUEUE_SIZE];
static uint8_t dtuQueueHead = 0;     // 出队索引
static uint8_t dtuQueueTail = 0;     // 入队索引
static uint8_t dtuQueueCount = 0;    // 当前队列中的消息数量
static uint32_t dtuLastSendTick = 0; // 上次发送时间戳（用�?100ms 间隔控制�?

/* Flash 配置存储
 * 同一页内分区存放（Flash 擦除以页为单位，任何一项写入都要整页重写）�?
 *   +0  : CONFIG_MAGIC(4B) + 对时配置字符�?32B)
 *   +64 : CENTER_MAGIC(4B) + 农场中心坐标字符�?32B) */
#define CONFIG_FLASH_ADDR 0x0800FC00 // Flash 最后一页（STM32F103C8T6 64KB�?
#define CONFIG_MAGIC 0xA5A5A5A5      // 有效数据标志
#define CENTER_FLASH_OFF 64          // 农场中心坐标区在配置页内的偏�?
#define CENTER_MAGIC 0x5A5A5A5A      // 中心坐标有效数据标志

/* bindDeviceId persistence: name record inside the active config page. */
#define NAME_RECORD_OFF 256
#define NAME_MAGIC      0x4E414D45u /* "NAME" */
#define NAME_COMMIT     0x4E414D31u /* "NAM1" */

/* txpower persistence: 16-byte record at +320 inside the config page
 * (magic, power, ~power, commit) �� inverse check instead of CRC. */
#define POWER_REC_OFF   320u
#define POWER_MAGIC     0x52574F50u /* "POWR" */
#define POWER_COMMIT    0x52573031u /* "POW1" */

typedef struct {
  uint32_t magic;    /* NAME_MAGIC */
  char     name[16]; /* device name, NUL terminated */
  uint32_t crc;      /* CRC32 over magic+name fields */
  uint32_t commit;   /* NAME_COMMIT, written last */
} CentenNameRecord;

/* 工作时间 62 进制字典
 * 配置串格�? "<周期分钟>,<工作时间2字符>"，如 "30,0M"
 * @note 顺序必须严格�? 数字 + 大写字母 + 小写字母（共 62 个）
 *       已用实配值核�? "00"=00:00~01:00, "0G"=00:00~17:00, "38"=10:00~20:00
 *       其中 "0G" 是唯一能区分大小写块顺序的判例，一旦对调结果就变成 01:00~21:00
 * @warning 顺序若与云端不一致，解码值会整体偏移，务必保持三块次序不�?*/
#define TIME_DICT "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

/* 待执行指令缓存：每个设备最多缓存一�?DTU 下发的指�?*/
#define PENDING_CMD_MAX_SLOTS 8 // 最多缓�?8 个设备的待执行指�?

typedef struct {
  uint8_t used;      // 该槽位是否被占用
  char deviceId[32]; // 目标设备 ID
  char cmd[32];      // 指令内容
  char val[32];      // 指令参数
  uint8_t hasExpiry;
  uint32_t generation;
  uint32_t expireTick; // 0=永久缓存；非0=到期时刻（JSON "time" 分钟数，超时不再下发�?
} PendingCmd_t;

static PendingCmd_t pendingCmdCache[PENDING_CMD_MAX_SLOTS];

/* gpstm 中继指令状态：收到 gpstm 后在有效期内下发 upgps 指令 */
static uint8_t gpstmActive = 0;      // gpstm 中继是否激活（0=未激�? 1=激活）
static uint32_t gpstmRecvTick = 0;   // 收到 gpstm 时的 HAL_GetTick() �?
static uint32_t gpstmTimeoutMin = 0; // gpstm 有效时长（分钟），来�?value 字段

/* lorasw 指令状态：LoRa 射频开�?
 *   1 = 射频工作，正常收�?
 *   0 = 射频深度休眠（rf_deepsleep），不再收发
 * @note 休眠只关 LoRa 芯片，MCU �?4G/DTU 继续运行�?
 *       所以云端仍可经 4G 下发 lorasw|1 远程唤醒
 * @warning 该状态只�?RAM，刻意不落盘：默认和重启后都�?可收�?�?
 *          掉电或复位即自动恢复收发，不会出�?关掉后醒不过�?的死状�?
 *          若要�?lorasw|0 跨重启保持，必须落盘，但那会引入上述风险 */
static uint8_t loraSwOn = 1;         // LoRa 射频开关（1=开, 0=关）

/* 工作区间自动控制状�?
 * @note 边沿触发：只�?进出工作区间"的那一刻自动开关射频，
 *       区间内部不强制，这样区间内手�?lorasw|0 关掉后会一直保持关闭，
 *       直到区间结束再进入才自动重开 —�?手动关在区间内才有效
 * @warning 工作区间优先级高�?lorasw 指令�?
 *          区间外一律休眠，且拒�?lorasw|1 开�?*/
static uint8_t prevInWorkWindow = 1;   // 上次是否在工作区间内（边沿检测）
static uint32_t workWinCheckTick = 0;  // 工作区间检测节流时间戳

/* 按键 K1/K2 相关代码已整体移除：本板未焊�?K1/K2，只保留复位�?
 * 移除原因�?MX_GPIO_Init �?K1/K2 引脚配置处的说明（悬空会伪造中断） */

/* USER CODE END PV */
/* Private function prototypes -----------------------------------------------*/
// 系统函数声明
void SystemClock_Config(void);         // 系统时钟配置函数
static void MX_GPIO_Init(void);        // GPIO 初始化函�?
static void MX_SPI1_Init(void);        // SPI1 初始化（LoRa 硬件 SPI�?
static void MX_USART1_UART_Init(void); // USART1 初始化（调试串口�?
static void MX_USART3_UART_Init(void); // USART3 初始化（DTU 通信�?
static void MX_NVIC_Init(void);        // NVIC 中断控制器初始化

/* USER CODE BEGIN PFP */
// 用户自定义函数声�?
static void printf_logo(void);   // 打印工程信息 Logo
static int device_name_valid(const char *name); /* bindDeviceId value check */
static void loadNameFromFlash(void);            /* bound name from Flash */
static void OnSlave(void);       // LoRa 从机接收处理函数
static void lora_rf_sleep(void);   // 关闭 LoRa 射频（深度休眠）
static uint8_t lora_rf_wake(void); // 唤醒 LoRa 射频并恢复接�?
/* do_tx_send() 已随 MODE_TX 一并删除（无触发源�?*/
static void sendToDtu(const char *loraStr, int rssi,
                      int snr); // 统一上报DTU（JSON格式，入队）
static void enqueueDtuMsg(const char *msg, int len); // 将消息加�?DTU 发送队�?
static void processDtuQueue(void);     // 处理 DTU 发送队列（每秒发一条）
static void loadConfigFromFlash(void); // �?Flash 加载配置 + 农场中心坐标
static uint8_t flash_save_all(void);   // 整页重写：对时配�?+ 农场中心坐标
static void sendCenterToDtu(void);  // 上报农场中心坐标 1|name|lat,lng|
static int center_coord_valid(double lat, double lng); // 经纬度合法性校�?
static void syncRelayCenter(void);      // 中心坐标字符�?>数值缓�?1e-5�?
static uint8_t restoreRelCoord(const char *in, char *out,
                               int outSize); // 相对偏移帧还原成绝对坐标
static void parseDtuCommand(char *buf);         // JSON 解析 DTU 下发指令
static uint32_t getCurrentTimestamp(void);      // 获取当前实时时间�?
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour,
                                    int min, int sec); // 日期转Unix时间�?
static uint32_t getConfigIntervalMs(void); // �?configStr 解析对时间隔（毫秒）
static int twoCharToIndex(const char *str); // 2 字符�?TIME_DICT 解成 62 进制�?
static int getWorkTimeFromConfig(void);    // 取配置里周期后的工作时间字段并解�?
static uint8_t indexToTimeWindow(int idx, uint8_t *outStart,
                                uint8_t *outEnd); // idx 转工作时间窗�?
static uint8_t workWindowEndAdj(uint8_t end); // 工作窗口结束小时修正（方案C�?
static uint8_t getWorkClockHour(void);        // 工作时钟：取当天小时(0~23)
static uint8_t isInWorkWindow(void);          // 当前是否处于工作区间�?
static void cachePendingCommand(const char *deviceId, const char *cmd,
                                const char *val, uint32_t timeMin); // 缓存待执行指�?
static uint8_t
checkPendingCommand(const char *deviceId); // 检查是否有缓存指令，返�?=�?0=�?
static uint8_t sendPendingCommand(
    const char *deviceId); // 发送缓存指令并清除，返�?=已发�?0=�?

static void serviceRadio(void);
static void scheduleLoraReply(const char *deviceId, uint8_t kind);
static int startLoraTx(const uint8_t *data, int len, int pendingSlot);
static void processDtuInput(void);
static void processDtuFrame(char *frame);
static void processDtuQueries(void);
static void dtuHardwareReset(void);
static int radioEnterRx(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* bindDeviceId value check: 1..15 chars, alnum plus '-' and '_'. */
static int device_name_valid(const char *name) {
  size_t len = 0;
  if (name == NULL || !*name) return 0;
  while (name[len]) {
    char c = name[len];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_'))
      return 0;
    len++;
  }
  return len < 16u; /* fits deviceName[16] with its NUL */
}

#ifdef __GNUC__
// GCC 编译器使�?__io_putchar 实现 printf 重定�?
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
// Keil ARMCC 编译器使�?fputc 实现 printf 重定�?
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
 * @brief  printf 底层输出函数重定�?
 * @note   �?printf 输出的每个字符通过 USART1 发送出�?
 * @param  ch: 要输出的字符
 * @retval 返回输出的字�?
 */
PUTCHAR_PROTOTYPE {
  // 通过 USART1 发送单个字符，超时时间 0xFFFF
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 2);
  return ch;
}

/**
 * @brief  打印工程信息 Logo
 * @note   在系统启动时调用，显示工程名称、版本、日期、硬件配置等信息
 */
static void printf_logo(void) {
  printf("CENTEN %s %luMHz\r\n", DEMO_VER,
         (unsigned long)(DEFAULT_FREQ / 1000000u));
}

/**
 * @brief  LoRa 从机接收处理函数
 * @note   功能说明�?
 *         1. 检查接收完成标志，若收到数据则读取 RSSI/SNR/数据内容
 *         2. 与上次接收的数据对比，标记为 [SAME] �?[DIFF]
 *         3. 打印接收信息（信号强度、信噪比、长度、内容）
 *         4. 处理接收错误（CRC 错误、超时）
 *         5. 重新进入单次超时接收模式（超时时�?5-15 秒）
 */
static int radioEnterRx(void) {
  return rf_enter_continous_rx() == RF_OK && !centen_radio_spi_failed();
}

static int startLoraTx(const uint8_t *data, int len, int pendingSlot) {
  uint32_t txTime = 0;
  if (!loraSwOn || radioTxActive || len <= 0)
    return 0;
  if (len > CENTEN_LORA_TX_MAX)
    len = CENTEN_LORA_TX_MAX; /* cap: keep one frame at 36 bytes on air */
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  if (rf_single_tx_data((uint8_t *)data, (uint8_t)len, &txTime) != RF_OK ||
      centen_radio_spi_failed()) {
    radioEnterRx();
    return 0;
  }
  radioTxActive = 1;
  radioTxStart = HAL_GetTick();
  radioTxPendingSlot = pendingSlot;
  if (pendingSlot >= 0) radioTxGeneration = pendingCmdCache[pendingSlot].generation;
  return 1;
}

static void scheduleLoraReply(const char *deviceId, uint8_t kind) {
  int i, slot = -1;
  for (i = 0; i < LORA_JOB_COUNT; i++) {
    if (loraJobs[i].used && strcmp(loraJobs[i].deviceId, deviceId) == 0) { slot = i; break; }
    if (!loraJobs[i].used && slot < 0) slot = i;
  }
  if (slot < 0) { radioJobsDropped++; return; }
  loraJobs[slot].used = 1;
  loraJobs[slot].kind = kind;
  strcpy(loraJobs[slot].deviceId, deviceId);
  loraJobs[slot].due = HAL_GetTick() +
      (kind == 0 ? LORA_SYNCTIME_DELAY_MS : LORA_CMD_DELAY_MS);
}

static void serviceRadio(void) {
  uint32_t now = HAL_GetTick();
  int i, selected = -1;
  char txData[CENTEN_COMMAND_FRAME_SIZE];
  int len;
  if (radioIrqPending && loraSwOn) {
    /* Clearing before processing preserves a newly arriving edge. */
    radioIrqPending = 0;
    rf_irq_process();
  }
  if (centen_radio_spi_failed()) {
    if (!radioRecoverPending) {
      radioRecoverPending = 1;
      radioRecoverTick = now;
      loraSwOn = 0;
      radioTxActive = 0;
      radioTxPendingSlot = -1;
      memset(loraJobs, 0, sizeof(loraJobs));
      printf("[LoRa] spi\r\n");
    }
  }
  if (radioRecoverPending) {
    if (now - radioRecoverTick >= 5000u && isInWorkWindow()) {
      radioRecoverTick = now;
      centen_radio_spi_clear();
      if (lora_rf_wake()) radioRecoverPending = 0;
    }
    return;
  }
  if (!loraSwOn) return;
  if (radioTxActive) {
    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE) {
      if (radioTxPendingSlot >= 0 && pendingCmdCache[radioTxPendingSlot].used &&
          pendingCmdCache[radioTxPendingSlot].generation == radioTxGeneration) {
        memset(&pendingCmdCache[radioTxPendingSlot], 0, sizeof(PendingCmd_t));
        printf("[PND] tx\r\n");
      }
    } else if (now - radioTxStart < 3000u) return;
    else {
      radioTxTimeouts++;
      printf("[LoRa] tmo\r\n");
    }
    radioTxActive = 0;
    radioTxPendingSlot = -1;
    rf_set_transmit_flag(RADIO_FLAG_IDLE);
    radioEnterRx();
  }
  if (radioPowerDirty) {
    if (rf_set_tx_power(localTxPower) != RF_OK || centen_radio_spi_failed()) return;
    radioPowerDirty = 0;
  }
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) return;
  for (i = 0; i < LORA_JOB_COUNT; i++) {
    if (!loraJobs[i].used || !centen_due(now, loraJobs[i].due)) continue;
    if (selected < 0 || (int32_t)(loraJobs[i].due - loraJobs[selected].due) < 0)
      selected = i;
  }
  if (selected < 0) return;
  /* An old wake window must never produce a late reply to the wrong uplink. */
  if (now - loraJobs[selected].due > 1000u) {
    loraJobs[selected].used = 0;
    radioJobsDropped++;
    return;
  }
  if (checkPendingCommand(loraJobs[selected].deviceId)) {
    sendPendingCommand(loraJobs[selected].deviceId);
  } else if (gpstmActive && now - gpstmRecvTick < gpstmTimeoutMin * 60000u) {
    len = centen_command_frame(txData, sizeof(txData), loraJobs[selected].deviceId, "upgps", "0");
    if (len > 0) startLoraTx((uint8_t *)txData, len, -1);
  } else if (dtuTimeValid) {
    uint32_t ts = getCurrentTimestamp(), sum = 0, t = ts;
    while (t) { sum += t % 10u; t /= 10u; }
    len = snprintf(txData, sizeof(txData), "%d|%lu|%02lu|%s", MSG_TYPE_SYN_TIME,
        (unsigned long)ts, (unsigned long)(sum % 100u), deviceName);
    if (len > 0 && len < (int)sizeof(txData)) startLoraTx((uint8_t *)txData, len, -1);
  }
  loraJobs[selected].used = 0;
}

static void OnSlave(void) {
  uint8_t local_buf[RX_LEN + 1u];
  uint8_t local_size, local_rssi;
  float local_snr;
  int msgType;
  char loraDevId[32];
  char reportBuf[RX_LEN + 32u];
  const char *p1, *p2;
  size_t idLen;
  if (!loraSwOn || radioTxActive) return;
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    int textSize = centen_text_payload_size(RxDoneParams.Payload, RxDoneParams.Size, RX_LEN);
    if (textSize <= 0) {
      radioRxRejected++;
      radioEnterRx();
      return;
    }
    local_size = (uint8_t)textSize;
    local_rssi = RxDoneParams.Rssi;
    local_snr = RxDoneParams.Snr;
    memcpy(local_buf, RxDoneParams.Payload, local_size);
    local_buf[local_size] = 0;
    if (local_buf[0] < '1' || local_buf[0] > '9' || local_buf[1] != '|') {
      radioRxRejected++; return;
    }
    msgType = local_buf[0] - '0';
    p1 = (const char *)local_buf + 2;
    p2 = strchr(p1, '|');
    idLen = p2 ? (size_t)(p2 - p1) : 0;
    if (!idLen || idLen >= sizeof(loraDevId)) { radioRxRejected++; return; }
    memcpy(loraDevId, p1, idLen); loraDevId[idLen] = 0;
    printf("[LoRa RX] rssi=%d snr=%d len=%u %s\r\n",
           local_rssi - 256, (int)local_snr, local_size, local_buf);
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    led1OnTick = HAL_GetTick(); led1On = 1;
    if (msgType == MSG_TYPE_TIME) {
      if (common_replaceTimeSegment((char *)local_buf, reportBuf, sizeof(reportBuf),
                                    getCurrentTimestamp(), NULL) == 0)
        sendToDtu(reportBuf, local_rssi - 256, (int)local_snr);
      else sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
    } else if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_UP_GPS) {
      if (restoreRelCoord((char *)local_buf, reportBuf, sizeof(reportBuf)))
        sendToDtu(reportBuf, local_rssi - 256, (int)local_snr);
      else sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
    } else if (msgType == MSG_TYPE_CONFIG || msgType == MSG_TYPE_READINFO)
      sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
    if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_TIME) {
      uint8_t kind = checkPendingCommand(loraDevId) ? 1u :
          (gpstmActive && HAL_GetTick() - gpstmRecvTick < gpstmTimeoutMin * 60000u ? 2u : 0u);
      if (kind || dtuTimeValid) scheduleLoraReply(loraDevId, kind);
    }
    /* Continuous RX is already active; do not stop it while waiting to reply. */
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR ||
             rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    radioEnterRx();
  }
}

/**
 * @brief  切换到接收模�?
 * @note   功能说明�?
 *         1. 设置工作模式�?MODE_RX
 *         2. 关闭 LED1（接收模式下 LED1 �?500ms 间隔闪烁�?
 *         3. 打印模式切换信息
 *         4. 进入单次超时接收模式，超时时�?5 �?
 */
/* =====================================================================
 * @brief  关闭 LoRa 射频，进入深度休�?
 * @note   只关 LoRa 芯片，MCU �?4G/DTU 继续运行�?
 *         因此云端仍可�?4G 下发 lorasw|1 远程唤醒
 * @warning 4G/DTU 必须始终保持工作，不得在此加入任�?4G 休眠/断电动作�?
 *          否则中继一旦被 lorasw|0 关掉，就再也收不到唤醒指令，只能现场断电
 *          4G 的周期任务（CSQ/nettime 查询、processDtuQueue、parseDtuCommand�?
 *          都跑在主循环里，不在被门禁挡住的 OnSlave() 内，休眠期间照常运行
 * ===================================================================== */
static void lora_rf_sleep(void) {
  memset(loraJobs, 0, sizeof(loraJobs));
  radioTxActive = 0;
  radioTxPendingSlot = -1;
  radioIrqPending = 0;
  rf_deepsleep();
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  loraSwOn = 0;
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET); // 关闭 LED1
  led1On = 0;
  printf("[RF] off\r\n");
}

/* =====================================================================
 * @brief  唤醒 LoRa 射频，恢复接�?
 * @note   PAN3029 深度休眠后寄存器掉电，必须重�?rf_init + 恢复参数�?
 *         不能直接 rf_enter_single_timeout_rx
 * @retval 1=唤醒成功, 0=唤醒失败
 * ===================================================================== */
static uint8_t lora_rf_wake(void) {
  int retry = 0;
  centen_radio_spi_clear();

  /* 与开机初始化一致：先确�?RST 为高，失败则复位后重试一�?*/
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  while (rf_init() != RF_OK) {
    if (retry >= 2) {
      printf("[LoRa] wake\r\n");
      goto failed;
    }
    retry++;
    centen_radio_spi_clear();
    printf("[LoRa] init %d\r\n", retry);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
  }

  if (rf_set_default_para() != RF_OK || rf_set_tx_power(localTxPower) != RF_OK ||
      centen_radio_spi_failed()) goto failed;
  rf_set_recv_flag(RADIO_FLAG_IDLE);
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  radioIrqPending = 0;
  // 恢复频率/SF/BW/功率等参�?
  if (!radioEnterRx()) goto failed; // 立即进入连续接收
  loraSwOn = 1;
  radioPowerDirty = radioRecoverPending = 0;
  printf("[RF] on\r\n");
  return 1;
failed:
  loraSwOn = 0;
  radioRecoverPending = 1;
  radioRecoverTick = HAL_GetTick();
  return 0;
}

/* do_tx_send()（每 3 秒发 "I LOVE YOU" 的测试发射）已删�?
 * 原因：它只由 MODE_TX 触发，而进�?MODE_TX 的唯一途径是按�?K2�?
 *       K2 移除后该模式再无任何入口，整个函数与 txMsg / lastTxTick /
 *       MODE_TX 均成死代码，一并移�?
 * 恢复方法：补回本函数�?txMsg / lastTxTick，在主循�?switch 里补
 *       case MODE_TX，并另给一个切换到 MODE_TX 的触发源 */

/**
 * @brief  将日期时间转换为 Unix 时间戳（秒）
 * @note   基于 1970-01-01 00:00:00 UTC，不考虑时区
 */
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour,
                                    int min, int sec) {
  uint32_t timestamp = 0;
  centen_datetime(year, month, day, hour, min, sec, &timestamp);
  return timestamp;
}

/**
 * @brief  �?configStr 解析对时间隔（毫秒）
 * @note   configStr 格式�?"30,0M"，取逗号前的数字作为分钟�?
 *         限制范围�?~60 分钟，超出则按边界值处�?
 * @retval 间隔时间（毫秒）
 */
static uint32_t getConfigIntervalMs(void) {
  int minutes = atoi(configStr);
  if (minutes < 5)
    minutes = 5;
  if (minutes > 60)
    minutes = 60;
  return (uint32_t)minutes * 60 * 1000;
}

/* =====================================================================
 * @brief  �?2 个字符按 TIME_DICT 解成 62 进制数�?
 * @note   与云�?twoCharToIndex() 算法一致：高位 * 62 + 低位
 * @param  str: 恰好 2 个字符的字符串（长度不是 2 直接返回 -1�?
 * @retval 0 ~ 3843 = 解码结果, -1 = 参数非法或字符不在字典内
 * ===================================================================== */
static int twoCharToIndex(const char *str) {
  const char *p0;
  const char *p1;
  int h, l;

  if (str == NULL || strlen(str) != 2)
    return -1;

  p0 = strchr(TIME_DICT, str[0]);
  p1 = strchr(TIME_DICT, str[1]);
  if (p0 == NULL || p1 == NULL)
    return -1;

  h = (int)(p0 - TIME_DICT);
  l = (int)(p1 - TIME_DICT);
  return h * 62 + l;
}

/* =====================================================================
 * @brief  �?idx 换算成一天里的工作时间窗口（小时级，最小间�?1 小时�?
 * @note   枚举规则：起始小�?s = 0~22，每�?s 对应 (23-s) 个窗口，
 *         总计 23+22+...+1 = 276 个，�?idx 合法范围�?0~275
 *         映射: outStart = s, outEnd = s + 1 + (idx - �?s 之前的累�?
 * @warning 不支持跨零点（如 22:00~06:00），因为算法保证 outEnd > outStart
 * @param  idx:      0~275
 * @param  outStart: 传出起始小时 0~22
 * @param  outEnd:   传出结束小时 1~23（区间为 [start, end) �?
 * @retval 1=换算成功, 0=idx 非法�?0 �?>275�?
 * ===================================================================== */
static uint8_t indexToTimeWindow(int idx, uint8_t *outStart, uint8_t *outEnd) {
  int sum = 0;

  if (idx < 0)
    return 0;

  for (int s = 0; s <= 23; s++) {
    int valid = 23 - (s + 1) + 1; /* = 23 - s */
    int off;

    if (valid <= 0)
      continue;

    if (idx < sum + valid) {
      off = idx - sum;
      *outStart = (uint8_t)s;
      *outEnd = (uint8_t)(s + 1 + off);
      return 1;
    }
    sum += valid;
  }
  return 0;
}

/* =====================================================================
 * @brief  工作窗口结束小时的修正（方案 C�?
 * @note   窗口统一按左闭右开 [start, end) 理解�?
 *         但编码能表达的最�?end 只有 23，那�?[0,23] 只覆盖到 22:59�?
 *         故特例：end==23 时按 24 算，�?[0,23] 表示整天 00:00~23:59
 *         其余窗口不受影响，[0,17] 仍是 00:00~17:00
 * @warning 显示�?将来判断是否工作"必须共用这一个函数，
 *          否则串口看到的和实际行为会对不上
 * ===================================================================== */
static uint8_t workWindowEndAdj(uint8_t end) {
  return (end == 23) ? 24u : end;
}

/* =====================================================================
 * @brief  工作时钟：取当天小时�?~23�?
 * @note   与日期无�?—�?timestamp % 86400 / 3600�?970 / 2026 都能�?
 * @warning 刻意不校�?dtuTimeValid�?
 *          未对上时 dtuTimestamp �?dtuSyncTick 初值均�?0�?
 *          本式退化为"开机经过的秒数"，即�?1970-01-01 00:00 为起点递增�?
 *          对上 4G 后自动切到真实时�?
 *          单独写一份是为了不污�?getCurrentTimestamp()（它仍返�?0�?
 *          避免未对上时把假 1970 时间戳上报给云端�?
 * ===================================================================== */
static uint8_t getWorkClockHour(void) {
  uint32_t ts = getCurrentTimestamp();
  return (uint8_t)((ts % 86400UL) / 3600UL);
}

/* =====================================================================
 * @brief  当前是否处于工作区间�?
 * @note   以下三种情况一�?视为在区间内"（射频常开），
 *         目的是任何异常都不能让中继睡死（宁可白耗电也不能丢数据）：
 *           �?4G 尚未对时 —�?没有可信时间，按 1970 基准判断会误�?
 *           �?配置里解析不出工作时间字�?
 *           �?idx 越界�?275，无对应窗口�?
 * @retval 1=在区间内(可工�?, 0=在区间外(应休�?
 * ===================================================================== */
static uint8_t isInWorkWindow(void) {
  uint8_t ws = 0;
  uint8_t we = 0;
  uint8_t hour;
  uint8_t end;
  int wt;

  if (!dtuTimeValid)
    return 1; /* �?未对时：不做休眠判断，保持常开 */

  wt = getWorkTimeFromConfig();
  if (wt < 0)
    return 1; /* �?配置无工作时间字�?*/

  if (!indexToTimeWindow(wt, &ws, &we))
    return 1; /* �?idx 越界 */

  hour = getWorkClockHour();
  end = workWindowEndAdj(we);

  return (hour >= ws && hour < end) ? 1 : 0;
}

/* =====================================================================
 * @brief  �?configStr �?周期后面的那个参�?并解码成工作时间
 * @note   configStr 格式 "<周期分钟>,<工作时间2字符>"，如 "30,0M"
 *         �?*第一个逗号之后**的两个字�?
 * @retval >=0 = 工作时间(原始�?, -1 = 配置里没有该字段或解码失�?
 * ===================================================================== */
static int getWorkTimeFromConfig(void) {
  const char *comma;
  char field[3];

  comma = strchr(configStr, ',');
  if (comma == NULL)
    return -1;

  if (comma[1] == '\0' || comma[2] == '\0')
    return -1;

  field[0] = comma[1];
  field[1] = comma[2];
  field[2] = '\0';

  return twoCharToIndex(field);
}

/* =====================================================================
 * @brief  打印工作时间的完整解码过程（供核对，暂不参与任何逻辑�?
 * @note   把每个字符在字典里的位置也打出来，方便一眼看出字典顺序对不对
 * ===================================================================== */
/**
 * @brief  获取当前实时时间戳（基于网络对时 + HAL 计时�?
 * @note   对时后以 dtuTimestamp 为基准，加上经过的秒�?
 */
static uint32_t getCurrentTimestamp(void) {
  if (!dtuTimeValid)
    return 0;
  {
    uint32_t elapsed = (HAL_GetTick() - dtuSyncTick) / 1000u;
    dtuTimestamp += elapsed;
    dtuSyncTick += elapsed * 1000u;
    return dtuTimestamp;
  }
}

/* =====================================================================
 * @brief  �?Flash 读出一个以 '\0' 结尾的字符串
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
 *         STM32F1 只能半字编程；用 i+1<=len 兜底�?
 *         避免读到源串 '\0' 之后的越界字�?
 * ===================================================================== */
/* Device-name flash record helpers: the record lives INSIDE the active
 * config page at +256 and is rewritten by flash_save_all in the same
 * erase/program pass (data+crc first, commit sentinel last, so a torn
 * write never validates). */
static int name_record_valid_at(uint32_t addr) {
  const CentenNameRecord *r = (const CentenNameRecord *)addr;
  return r->magic == NAME_MAGIC && r->commit == NAME_COMMIT &&
      memchr(r->name, 0, sizeof(r->name)) != NULL &&
      device_name_valid(r->name) &&
      r->crc == centen_crc32(r, offsetof(CentenNameRecord, crc));
}

static void prepare_name_record(CentenNameRecord *r) {
  memset(r, 0, sizeof(*r));
  r->magic = NAME_MAGIC;
  strncpy(r->name, deviceName, sizeof(r->name) - 1u);
  r->crc = centen_crc32(r, offsetof(CentenNameRecord, crc));
  r->commit = NAME_COMMIT;
}

static int power_record_valid_at(uint32_t addr) {
  const uint32_t *p = (const uint32_t *)addr;
  return p[0] == POWER_MAGIC && p[3] == POWER_COMMIT &&
      p[2] == ~p[1] && p[1] >= 1u && p[1] <= 23u;
}

static uint8_t flash_program_hwords(uint32_t addr, const uint8_t *bytes,
                                    uint32_t len) {
  uint32_t off;
  for (off = 0; off < len; off += 2u) {
    uint16_t value = (uint16_t)(bytes[off] | ((uint16_t)bytes[off + 1u] << 8));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + off, value) != HAL_OK)
      return 0;
  }
  return 1;
}

static uint8_t flash_save_all(void) {
  CentenConfigRecord record;
  CentenNameRecord nameRec;
  uint32_t powerRec[4];
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError, address;
  const uint8_t *bytes = (const uint8_t *)&record;
  const uint8_t *nbytes = (const uint8_t *)&nameRec;
  uint8_t ok = 1;
  if (!centen_config_valid(configStr)) return 0;
  if (configActiveAddr) {
    const CentenConfigRecord *old = (const CentenConfigRecord *)configActiveAddr;
    if (centen_record_valid(old) && strcmp(old->config, configStr) == 0 &&
        strcmp(old->center, centerStr) == 0 &&
        name_record_valid_at(configActiveAddr + NAME_RECORD_OFF) &&
        power_record_valid_at(configActiveAddr + POWER_REC_OFF)) {
      const CentenNameRecord *on =
          (const CentenNameRecord *)(configActiveAddr + NAME_RECORD_OFF);
      if (strcmp(on->name, deviceName) == 0 &&
          (uint32_t)localTxPower ==
              *(const uint32_t *)(configActiveAddr + POWER_REC_OFF + 4u))
        return 1;
    }
  }
  address = configActiveAddr == CONFIG_PAGE_A ? CONFIG_PAGE_B : CONFIG_PAGE_A;
  centen_record_prepare(&record, configStr, centerStr, configSequence + 1u);
  prepare_name_record(&nameRec);
  powerRec[0] = POWER_MAGIC;
  powerRec[1] = localTxPower;
  powerRec[2] = ~localTxPower;
  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = address;
  eraseInit.NbPages = 1;
  if (HAL_FLASH_Unlock() != HAL_OK) return 0;
  if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) ok = 0;
  if (ok && !flash_program_hwords(address, bytes,
                                  offsetof(CentenConfigRecord, commit))) ok = 0;
  if (ok && memcmp((const void *)address, &record, offsetof(CentenConfigRecord, commit)) != 0) ok = 0;
  if (ok && !flash_program_hwords(address + NAME_RECORD_OFF, nbytes,
                                  offsetof(CentenNameRecord, commit))) ok = 0;
  if (ok && memcmp((const void *)(address + NAME_RECORD_OFF), &nameRec,
                   offsetof(CentenNameRecord, commit)) != 0) ok = 0;
  if (ok && !flash_program_hwords(address + POWER_REC_OFF, (const uint8_t *)powerRec, 12u)) ok = 0;
  if (ok && memcmp((const void *)(address + POWER_REC_OFF), powerRec, 12u) != 0) ok = 0;
  if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
      address + offsetof(CentenConfigRecord, commit), CENTEN_CONFIG_COMMIT) != HAL_OK) ok = 0;
  if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
      address + NAME_RECORD_OFF + offsetof(CentenNameRecord, commit), NAME_COMMIT) != HAL_OK) ok = 0;
  if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
      address + POWER_REC_OFF + 12u, POWER_COMMIT) != HAL_OK) ok = 0;
  HAL_FLASH_Lock();
  if (!ok || !centen_record_valid((const CentenConfigRecord *)address) ||
      !name_record_valid_at(address + NAME_RECORD_OFF) ||
      !power_record_valid_at(address + POWER_REC_OFF)) {
    printf("[FLASH] fail\r\n");
    return 0;
  }
  configActiveAddr = address;
  configSequence = record.sequence;
  printf("[FLASH] seq=%lu\r\n", (unsigned long)configSequence);
  return 1;
}

/**
 * @brief  �?Flash 加载配置信息 + 农场中心坐标
 * @note   开机时调用，如�?Flash 中有有效数据则加�?
 */
static void loadConfigFromFlash(void) {
  const CentenConfigRecord *a = (const CentenConfigRecord *)CONFIG_PAGE_A;
  const CentenConfigRecord *b = (const CentenConfigRecord *)CONFIG_PAGE_B;
  const CentenConfigRecord *selected = NULL;
  double lat, lng;
  if (centen_record_valid(a)) selected = a;
  if (centen_record_valid(b) && (!selected || (int32_t)(b->sequence - selected->sequence) > 0)) selected = b;
  if (selected) {
    strcpy(configStr, selected->config); strcpy(centerStr, selected->center);
    configActiveAddr = (uint32_t)selected; configSequence = selected->sequence;
  } else {
    char legacy[32];
    /* Upgrade compatibility: the previous one-page format stays untouched
     * until a new record has been committed in page A. */
    if (*(volatile uint32_t *)CONFIG_FLASH_ADDR == CONFIG_MAGIC) {
      flash_read_str(CONFIG_FLASH_ADDR + 4u, legacy, sizeof(legacy));
      if (centen_config_valid(legacy)) strcpy(configStr, legacy);
    }
    if (*(volatile uint32_t *)(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF) == CENTER_MAGIC) {
      flash_read_str(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF + 4u, legacy, sizeof(legacy));
      if (centen_center_parse(legacy, &lat, &lng)) strcpy(centerStr, legacy);
    }
  }
  syncRelayCenter();
  loraSwOn = 1;
  printf("[FLASH] cfg=%s center=%s sequence=%lu\r\n", configStr, centerStr,
         (unsigned long)configSequence);
}

/* Device name + tx power: prefer records on the active config page, else
 * scan both pages (a page switch during the last save moves them). */
static void loadNameFromFlash(void) {
  uint32_t pages[2];
  int i, bound = 0, pset = 0;
  pages[0] = configActiveAddr ? configActiveAddr : CONFIG_PAGE_A;
  pages[1] = CONFIG_PAGE_B;
  for (i = 0; i < 2 && (!bound || !pset); i++) {
    if (!bound && name_record_valid_at(pages[i] + NAME_RECORD_OFF)) {
      strcpy(deviceName,
             ((const CentenNameRecord *)(pages[i] + NAME_RECORD_OFF))->name);
      bound = 1;
    }
    if (!pset && power_record_valid_at(pages[i] + POWER_REC_OFF)) {
      localTxPower = (uint8_t)*(const uint32_t *)(pages[i] + POWER_REC_OFF + 4u);
      pset = 1;
    }
  }
  printf("[FLASH] %s %s p=%u\r\n",
         deviceName, bound ? "bound" : "default", localTxPower);
}

/**
 * @brief  将消息加�?DTU 发送队列（环形缓冲区）
 * @param  msg: 消息数据指针
 * @param  len: 消息长度
 * @note   队列满时丢弃最早的消息（覆盖队头）
 */
static void enqueueDtuMsg(const char *msg, int len) {
  if (len <= 0 || len >= DTU_MSG_MAX_LEN)
    return;

  if (dtuQueueCount >= DTU_QUEUE_SIZE) {
    // 队列满，丢弃最早的一条（队头前移�?
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
    dtuQueueDropped++;
    printf("[DTU Q] full, drop old\r\n");
  }
  // 拷贝消息到队�?
  memcpy(dtuQueue[dtuQueueTail].buf, msg, len);
  dtuQueue[dtuQueueTail].buf[len] = '\0';
  dtuQueue[dtuQueueTail].len = (uint16_t)len;
  dtuQueueTail = (dtuQueueTail + 1) % DTU_QUEUE_SIZE;
  dtuQueueCount++;
}

/**
 * @brief  处理 DTU 发送队列：�?2 秒从队头取出一条消息发�?
 * @note   在主循环中调用，非阻塞。间�?2 秒：JSON 上报（对�?配置/
 *         rst_dtu）相邻发出时 DTU 需要时间转发到服务器——之�?500ms
 *         间隔曾把前一条挤丢（对时上报失败、配置上报成功）�?
 *         csq/nettime 等短命令 2 秒间隔同样安�?
 */
static void processDtuQueue(void) {
  uint32_t now = HAL_GetTick();
  HAL_StatusTypeDef status;
  DtuQueueItem_t *item;
  if (!dtuQueueCount || now - dtuLastSendTick < 2000u) return;
  /* A deferred attempt is not a send. Do not restart the two-second throttle
   * until a UART transmission is actually attempted. */
  if (dtuQueryStage || dtuQueryPending || now - dtuQueryTick < 500u ||
      now - dtuRecoveryStart < 5000u) return;
  item = &dtuQueue[dtuQueueHead];
  dtuLastSendTick = now;
  status = HAL_UART_Transmit(&huart3, (uint8_t *)item->buf, item->len, 100);
  if (status == HAL_OK)
    status = HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 20);
  if (status != HAL_OK) {
    dtuTxFailures++;
    printf("[DTU TX FAIL] status=%u err=%lu q=%u; retained\r\n",
           (unsigned int)status, (unsigned long)huart3.ErrorCode, dtuQueueCount);
    return;
  }
  printf("[DTU Q TX] %s\r\n", item->buf);
  dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
  dtuQueueCount--;
}

/**
 * @brief  统一上报数据�?DTU（JSON格式�?
 * @note   数据先入队，�?processDtuQueue() �?2 秒发送一�?
 * @param  loraStr: lorainfo 字段内容
 * @param  rssi:    RSSI 信号强度
 * @param  snr:     SNR 信噪�?
 */
static void sendToDtu(const char *loraStr, int rssi, int snr) {
  static char txBuf[DTU_MSG_MAX_LEN];
  static char escaped[(RX_LEN + 32u) * 6u + 1u];
  int len;
  if (centen_json_escape(loraStr, escaped, sizeof(escaped)) < 0) {
    dtuQueueDropped++; return;
  }
  len = snprintf(txBuf, sizeof(txBuf),
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
                     escaped, (char *)deviceName,
                     (unsigned long)getCurrentTimestamp(), rssi, snr);

  if (len > 0 && len < (int)sizeof(txBuf)) {
    enqueueDtuMsg(txBuf, len);
    printf("[DTU Q] %u/%u len=%d\r\n", dtuQueueCount, DTU_QUEUE_SIZE, len);
  } else {
    dtuQueueDropped++;
    printf("[DTU Q DROP] JSON len=%d\r\n", len);
  }
}

/**
 * @brief  获取到网络时间后，上报一次对时信息给 DTU
 * @note   数据格式: 2|deviceName|timestamp|battery|count
 */
static void sendTimeSyncToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%lu|%d|%lu", MSG_TYPE_TIME,
           deviceName, (unsigned long)getCurrentTimestamp(), batteryValue,
           (unsigned long)rtcSendCount++);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/**
 * @brief  开机网络正常后，上报一次配置信息给 DTU
 * @note   数据格式: 6|deviceName|config_str|count
 */
static void sendConfigToDtu(void) {
  char dataStr[64];
  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|%lu", MSG_TYPE_CONFIG,
           deviceName, configStr, (unsigned long)rtcSendCount++);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/* =====================================================================
 * @brief  上报农场中心坐标�?DTU
 * @note   数据格式: 1|deviceName|lat,lng|
 *         （MSG_TYPE_GPS = 1，与终端 GPS 上报类型一致）
 *         未设置过坐标时第 3 段上�?"0,0"，保证帧结构固定�?3 段，
 *         云端/小程�?split('|') 不会拿到空串
 * ===================================================================== */
static void sendCenterToDtu(void) {
  char dataStr[64];
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|", MSG_TYPE_GPS, deviceName,
           coord);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/* =====================================================================
 * @brief  中心坐标字符�?-> 数值缓存（1e-5 度）
 * @note   加载 Flash 后、mapcenter 设置后调用；解析失败置无�?
 * ===================================================================== */
static void syncRelayCenter(void) {
  double la = 0.0, lo = 0.0;

  if (centerStr[0] != '\0' && centen_center_parse(centerStr, &la, &lo)) {
    relayCenterLat = (int32_t)(la * 100000.0 + (la >= 0.0 ? 0.5 : -0.5));
    relayCenterLng = (int32_t)(lo * 100000.0 + (lo >= 0.0 ? 0.5 : -0.5));
    relayCenterValid = 1;
  } else {
    relayCenterValid = 0;
  }
}

/* =====================================================================
 * @brief  1e-5 度整�?-> "xx.xxxxx" 单段字符串（与终�?deg1e5ToStr 同格式）
 * ===================================================================== */
static void coord1e5ToStr(int32_t v, char *buf) {
  uint32_t a;

  if (v < 0) {
    a = (uint32_t)(-v);
    sprintf(buf, "-%lu.%05lu", (unsigned long)(a / 100000u),
            (unsigned long)(a % 100000u));
  } else {
    a = (uint32_t)v;
    sprintf(buf, "%lu.%05lu", (unsigned long)(a / 100000u),
            (unsigned long)(a % 100000u));
  }
}

/* =====================================================================
 * @brief  还原子设�?相对中心偏移"上报�?
 * @param  in:      收到的帧 "1|dev|dlat,dlng|sec|count|"
 * @param  out:     还原�?  "1|dev|lat,lng|sec|count|"
 * @retval 1=已还�? 0=不需还原（坐标段含小数点=绝对坐标/0.0,0.0，或无中心）
 * @note   判定规则：坐标段�?'.' = 相对偏移（放�?1e5 的整数）�?
 *         加回中心坐标还原绝对坐标后再�?DTU；云端只看到绝对坐标
 * ===================================================================== */
static uint8_t restoreRelCoord(const char *in, char *out, int outSize) {
  const char *p1, *p2, *p3, *comma;
  char seg[24];
  char latBuf[16], lngBuf[16];
  int n;

  if (!relayCenterValid)
    return 0;

  p1 = strchr(in, '|');                  /* 类型段后 */
  p2 = p1 ? strchr(p1 + 1, '|') : NULL;  /* 设备名段�?= 坐标段起�?*/
  p3 = p2 ? strchr(p2 + 1, '|') : NULL;  /* 坐标段后 */
  if (!p3)
    return 0;

  n = (int)(p3 - (p2 + 1));
  if (n <= 0 || n >= (int)sizeof(seg))
    return 0;
  memcpy(seg, p2 + 1, n);
  seg[n] = '\0';
  if (strchr(seg, '.'))                  /* 绝对坐标/0.0,0.0：不还原 */
    return 0;
  comma = strchr(seg, ',');
  if (!comma)
    return 0;

  {
    char *end;
    long dlat = strtol(seg, &end, 10), dlng;
    int32_t lat, lng;
    if (end != comma || end == seg || dlat < -18000000L || dlat > 18000000L) return 0;
    dlng = strtol(comma + 1, &end, 10);
    if (end == comma + 1 || *end || dlng < -36000000L || dlng > 36000000L) return 0;
    lat = relayCenterLat + (int32_t)dlat;
    lng = relayCenterLng + (int32_t)dlng;
    if (lat < -9000000 || lat > 9000000 || lng < -18000000 || lng > 18000000) return 0;
    coord1e5ToStr(lat, latBuf); coord1e5ToStr(lng, lngBuf);
  }
  int result = snprintf(out, outSize, "%.*s|%.*s|%s,%s|%s",
           (int)(p1 - in), in,           /* 类型�?*/
           (int)(p2 - (p1 + 1)), p1 + 1, /* 设备名段 */
           latBuf, lngBuf,
           p3 + 1);                      /* 尾段（搜星秒数|计数|�?*/
  return result > 0 && result < outSize;
}

/**
 * @brief  缓存待执行指令（每个设备最多一条，新指令替换旧指令�?
 * @param  deviceId: 目标设备 ID
 * @param  cmd:      指令内容
 * @param  val:      指令参数
 * @param  timeMin:  时效分钟数（JSON "time" 字段�?=永久缓存�?
 * @note   timeMin>0 时缓�?expireTick=now+time 分钟，到期未下发则清�?
 */
static void cachePendingCommand(const char *deviceId, const char *cmd,
                                const char *val, uint32_t timeMin) {
  int freeSlot = -1;

  /* 时效上限 120 分钟：云端传更大值按 120 处理�?=无时效永久缓�?*/
  if (timeMin > 120)
    timeMin = 120;

  uint32_t expire =
      timeMin ? (HAL_GetTick() + timeMin * 60000UL) : 0;

  /* 先查找是否已有该设备的缓存槽�?*/
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      /* 已有该设备的缓存，直接替换（时效同时刷新�?*/
      strncpy(pendingCmdCache[i].cmd, cmd, sizeof(pendingCmdCache[i].cmd) - 1);
      pendingCmdCache[i].cmd[sizeof(pendingCmdCache[i].cmd) - 1] = '\0';
      strncpy(pendingCmdCache[i].val, val, sizeof(pendingCmdCache[i].val) - 1);
      pendingCmdCache[i].val[sizeof(pendingCmdCache[i].val) - 1] = '\0';
      pendingCmdCache[i].expireTick = expire;
      pendingCmdCache[i].hasExpiry = timeMin != 0;
      pendingCmdCache[i].generation = ++pendingGeneration;
      return;
    }
    if (!pendingCmdCache[i].used && freeSlot < 0)
      freeSlot = i;
  }

  /* 未找到已有槽位，使用空闲槽位 */
  if (freeSlot >= 0) {
    pendingCmdCache[freeSlot].used = 1;
    pendingCmdCache[freeSlot].expireTick = expire;
    pendingCmdCache[freeSlot].hasExpiry = timeMin != 0;
    pendingCmdCache[freeSlot].generation = ++pendingGeneration;
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
  } else {
    printf("[PENDING CMD] full, drop=%s\r\n", deviceId);
  }
}

/**
 * @brief  清理超时的缓存指令（�?30 秒扫一遍）
 * @note   JSON �?"time" 分钟时效的指令，到期未匹配到下发对象即清除，
 *         不再下发给子设备；time=0 的永久缓存不受影�?
 */
static void cleanupExpiredPendingCmds(void) {
  static uint32_t lastCleanTick = 0;
  uint32_t now = HAL_GetTick();

  if (now - lastCleanTick < 30000)
    return;
  lastCleanTick = now;

  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used && pendingCmdCache[i].hasExpiry &&
        (int32_t)(now - pendingCmdCache[i].expireTick) >= 0) {
      printf("[PENDING CMD] expired: %s %s\r\n", pendingCmdCache[i].deviceId,
             pendingCmdCache[i].cmd);
      pendingCmdCache[i].used = 0;
      memset(&pendingCmdCache[i], 0, sizeof(PendingCmd_t));
    }
  }
}

/**
 * @brief  检查并处理指定设备的待执行缓存指令
 * @note   �?LoRa 收到该设备的上报数据时调用，打印缓存指令并清除缓�?
 * @param  deviceId: 上报数据的设�?ID
 */
static uint8_t checkPendingCommand(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      /* 时效已过：清除且不下�?*/
      if (pendingCmdCache[i].hasExpiry &&
          (int32_t)(HAL_GetTick() - pendingCmdCache[i].expireTick) >= 0) {
        printf("[PENDING CMD] expired: %s %s\r\n", deviceId,
               pendingCmdCache[i].cmd);
        pendingCmdCache[i].used = 0;
        memset(&pendingCmdCache[i], 0, sizeof(PendingCmd_t));
        continue;
      }
      return 1; /* 有缓存指�?*/
    }
  }
  return 0; /* 无缓存指�?*/
}

/**
 * @brief  通过 LoRa 发送缓存指令给目标设备并清除缓�?
 * @param  deviceId: 目标设备 ID
 * @retval 1=已发�? 0=无缓�?
 */
static uint8_t sendPendingCommand(const char *deviceId) {
  int i;
  char txData[CENTEN_COMMAND_FRAME_SIZE];
  if (!checkPendingCommand(deviceId)) return 0;
  for (i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    int len;
    if (!pendingCmdCache[i].used || strcmp(pendingCmdCache[i].deviceId, deviceId) != 0) continue;
    len = centen_command_frame(txData, sizeof(txData), deviceId,
                               pendingCmdCache[i].cmd, pendingCmdCache[i].val);
    if (len < 0) { printf("[PENDING CMD] invalid frame retained\r\n"); return 0; }
    return (uint8_t)startLoraTx((uint8_t *)txData, len, i);
  }
  return 0;
}

/* =====================================================================
 * @brief  校验经纬度是否在合法地理范围�?
 * @retval 1=合法, 0=非法
 * ===================================================================== */
static int center_coord_valid(double lat, double lng) {
  return lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0;
}

/**
 * @brief  PA3 脉冲复位 DTU 模块（高电平有效：拉�?100ms 后拉回低�?
 * @note   实测确认极性：平时低（不复位），拉�?= 复位脉冲
 */
static void dtuHardwareReset(void) {
  printf("[DTU] RST dtu\r\n");
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET);
  /* time state is tied to the old DTU session: force re-sync in fast mode
   * (5s query) after re-registration, don't carry stale/fake time over */
  dtuTimeValid = 0;
  dtuNetTimeFailCount = 0;
  dtuQueryStage = dtuQueryPending = dtuQueryAnswered = 0;
  syncReportPending = 0;
  dtuRecoveryStart = dtuCsqTick = HAL_GetTick();
  dtuPingGraceStart = dtuRecoveryStart;
  dtuPingGot = 0;
  dtuCsqAnswerTick = 0;
  centenDtuRxRecover = 1;
  dtuResetCount++;
}

/**
 * @brief  JSON 解析 DTU 下发的指�?
 * @note   从外�?JSON 提取 cominfo 字段，反转义后解析内�?JSON
 *         内部格式: {"cmd":"xxx","value":"xxx","deviceId":"xxx"}
 */
static void parseDtuCommand(char *buf) {
  CentenCommand parsed;
  char *cmd = parsed.cmd, *devId = parsed.deviceId, *val = parsed.value;
  uint32_t timeMin;
  if (!centen_parse_command(buf, &parsed)) {
    dtuParseRejected++;
    printf("[DTU] bad cmd\r\n");
    return;
  }
  timeMin = parsed.timeMin;

  /* bindDeviceId renames THIS relay. Self-executed regardless of the
   * deviceId field (the cloud may not know the current/default name);
   * value = new device name, validated and persisted in Flash. */
  if (strcmp(cmd, "bindDeviceId") == 0) {
    char previous[16];
    const char *status;
    if (!device_name_valid(val)) {
      status = "invalid";
    } else if (strcmp(val, deviceName) == 0) {
      status = "unchanged";
    } else {
      strcpy(previous, deviceName);
      strncpy(deviceName, val, sizeof(deviceName) - 1u);
      deviceName[sizeof(deviceName) - 1u] = '\0';
      if (flash_save_all()) status = "saved";
      else {
        strcpy(deviceName, previous);
        status = "save failed";
      }
    }
    printf("[DTU] bindid %s %s\r\n", val, status);
    {
      char ackStr[48];
      snprintf(ackStr, sizeof(ackStr), "8|%s|bindid", deviceName);
      sendToDtu(ackStr, 0, 0);
    }
    return;
  }
  if (strcmp(devId, deviceName) != 0 && strcmp(cmd, "txpower") != 0) {
    cachePendingCommand(devId, cmd, val, timeMin);
    return;
  }

  /* Step 4: 执行指令 */
  if (strcmp(cmd, "relayreboot") == 0) {
    printf("[DTU] reboot\r\n");
    HAL_Delay(500);
    NVIC_SystemReset();
  } else if (strcmp(cmd, "mapcenter") == 0) {
    /* 设置农场中心坐标：先验格式和地理范围，只有合法才写内�?+ Flash�?
     * �?double 解析，避�?109.39087 这类 8 位有效数字被 float 截断�?*/
    double la = 0.0, lo = 0.0;

    if (centen_center_parse(val, &la, &lo) && center_coord_valid(la, lo)) {
      /* 归一化成 5 位小数存储：帧长固定，且天然不含 " \ | 等会破坏
       * JSON 拼接的字�?*/
      char previous[32];
      strcpy(previous, centerStr);
      char latText[16], lngText[16];
      coord1e5ToStr((int32_t)(la * 100000.0 + (la >= 0.0 ? 0.5 : -0.5)), latText);
      coord1e5ToStr((int32_t)(lo * 100000.0 + (lo >= 0.0 ? 0.5 : -0.5)), lngText);
      snprintf(centerStr, sizeof(centerStr), "%s,%s", latText, lngText);
      syncRelayCenter();          /* 同步数值缓存（相对偏移还原用） */
      if (!flash_save_all()) { strcpy(centerStr, previous); syncRelayCenter(); }
      printf("[DTU] center %s\r\n", centerStr);
    } else {
      /* 非法：不写内存、不�?Flash，保持上一次的合法�?*/
      printf("[DTU] center bad\r\n");
    }
    /* 无论本次是否合法，都上报当前内存中的坐标�?
     * 合法  -> 回新坐标；非�?-> 回上一个合法值（未设过则 "0,0"�?
     * 格式: 1|deviceName|lat,lng| */
    sendCenterToDtu();
  } else if (strcmp(cmd, "debug") == 0) {
    /* debug: value=2 时复�?DTU 模块（远程手动触发异常恢复） */
    uint32_t debugVal;
    if (!centen_parse_uint(val, 0, 2, &debugVal)) return;
    if (debugVal == 2) {
      dtuHardwareReset();
    } else {
      printf("[DTU] dbg=%d\r\n", debugVal);
    }
  } else if (strcmp(cmd, "A") == 0) {
    if (centen_config_valid(val)) {
      char previous[32];
      strcpy(previous, configStr);
      strncpy(configStr, val, sizeof(configStr) - 1);
      configStr[sizeof(configStr) - 1] = '\0';
      if (!flash_save_all()) strcpy(configStr, previous);
      printf("[DTU] cfg %s\r\n", configStr);
      sendConfigToDtu();
    } else {
      printf("[DTU] badval\r\n");
    }
  } else if (strcmp(cmd, "gpstm") == 0) {
    /* gpstm 中继指令：仅�?deviceId 匹配本机时激活，记录当前时间和有效时�?*/
    if (strcmp(devId, (char *)deviceName) != 0) {
      printf("[DTU] gskip\r\n");
      return;
    }
    if (!centen_parse_uint(val, 0, 120, &gpstmTimeoutMin)) return;
    gpstmActive = 1;
    gpstmRecvTick = HAL_GetTick();
    if (gpstmTimeoutMin == 0)
      gpstmTimeoutMin = 30; // 默认 30 分钟
    printf("[DTU] gpstm %lu\r\n", (unsigned long)gpstmTimeoutMin);
  } else if (strcmp(cmd, "lorasw") == 0) {
    /* lorasw 中继 LoRa 射频开�?
     *   value=1 �?开启射频，恢复收发
     *   value=0 �?关闭射频，进入深度休眠（MCU �?4G/DTU 仍工作）
     * 状态只存在�?RAM，不�?Flash：默认就是可收发�?
     * 重启后同样回到可收发（掉�?复位相当于自动恢复）
     * 云端可经 4G 下发 lorasw|1 远程唤醒 */
    if (strcmp(devId, (char *)deviceName) != 0) {
      printf("[DTU] lskip\r\n");
      return;
    }

    uint32_t enabled;
    if (!centen_parse_uint(val, 0, 1, &enabled)) return;
    if (enabled) {
      /* 1 �?唤醒
       * @warning 工作时间优先：不在工作区间内时一律拒绝开启，
       *          lorasw 只能在区间内生效，不能越权把区间外的射频打开 */
      if (!isInWorkWindow()) {
        printf("[DTU] lorasw no\r\n");
      } else if (loraSwOn) {
        printf("[DTU] rf on\r\n");
      } else {
        lora_rf_wake();
      }
    } else {
      /* 0 �?休眠（任何时候都允许关闭，不受工作区间限制） */
      radioRecoverPending = 0;
      centen_radio_spi_clear();
      if (!loraSwOn) {
        printf("[DTU] rf off\r\n");
      } else {
        lora_rf_sleep();
      }
    }
  } else if (strcmp(cmd, "txpower") == 0) {
    /* txpower: 走到这里必是本机（子设备目标已在 Step 3 缓存）—�?
     * 只做一件事：改中继自己的发射功率。不缓存自己（避免死缓存占槽），
     * 不重�?CSQ/nettime（旧逻辑遗留）；功率不落盘，重启恢复默认 */
    uint32_t p;
    if (!centen_parse_uint(val, 1, DEFAULT_FREQ < 565000000u ? 23u : 22u, &p)) return;
    localTxPower = (uint8_t)p;
    radioPowerDirty = 1;
    if (loraSwOn && !radioTxActive &&
        rf_set_tx_power((uint8_t)p) == RF_OK && !centen_radio_spi_failed())
      radioPowerDirty = 0; /* applied now; dirty retries on SPI hiccup */
    printf("[DTU] txp %d\r\n", p);
    if (!flash_save_all())
      printf("[DTU] save fail\r\n");
    return;
  } else if (strcmp(cmd, "realyname") == 0) {
    /* realyname 更换设备名字 */
    /* value 格式: "v3-10=v3-11"  �? 目标设备=v3-10, 新名�?v3-11 */
    char *eq = strchr(val, '=');
    if (eq != NULL && eq != val && eq[1] != 0) {
      *eq = '\0';
      char *targetDevId = val;
      char *newName = eq + 1;
      printf("[DTU] rn %s=%s\r\n", targetDevId, newName);
      cachePendingCommand(targetDevId, cmd, newName, timeMin);
    } else {
      printf("[DTU] realyname bad\r\n");
    }


  } else if (strcmp(cmd, "readinfo") == 0) {
    /* readinfo: split into three frames (server single-frame length limit).
     * Frame1 main:  8|name|INFO|countT|freqHZ|powerdbm|version|txCount|
     *               relay does not track node count; count field fixed 0
     * Frame2 ICCID: 8|name|ICCID|<20-digit SIM id>|
     * Frame3 IMSI:  8|name|IMSI|<15-digit IMSI>|
     * If ICCID/IMSI not yet queried after boot the field is empty;
     * re-issue readinfo to refresh. */
    char infoStr[48];
    printf("[H] uart=%lu parse=%lu tx=%lu qd=%lu rst=%lu rfto=%lu rfdr=%lu\r\n",
      (unsigned long)centenDtuRing.dropped, (unsigned long)(dtuParseRejected + dtuFrame.rejected),
      (unsigned long)dtuTxFailures, (unsigned long)dtuQueueDropped, (unsigned long)dtuResetCount,
      (unsigned long)radioTxTimeouts, (unsigned long)(radioRxRejected + radioJobsDropped));
    snprintf(infoStr, sizeof(infoStr), "%d|%s|INFO|%dT|%luHZ|%ddbm|%s|%lu|",
             MSG_TYPE_READINFO, deviceName, 0,
             (unsigned long)(DEFAULT_FREQ / 1000000u),
             (int)localTxPower, DEMO_VER, (unsigned long)rtcSendCount);
    sendToDtu(infoStr, dtuCsqRssi, 0);
    snprintf(infoStr, sizeof(infoStr), "%d|%s|ICCID|%s|", MSG_TYPE_READINFO,
             deviceName, dtuIccidStr);
    sendToDtu(infoStr, dtuCsqRssi, 0);
    snprintf(infoStr, sizeof(infoStr), "%d|%s|IMSI|%s|", MSG_TYPE_READINFO,
             deviceName, dtuImsiStr);
    sendToDtu(infoStr, dtuCsqRssi, 0);
  } else {
    printf("[DTU] ? %s\r\n", cmd);
  }
}

/* USER CODE END 0 */

/**
 * @brief  应用程序主入口函�?
 * @retval int (实际不会返回)
 * @note   主函数执行流程：
 *         1. HAL 库初始化（复位所有外设、初始化 Flash �?SysTick�?
 *         2. 系统时钟配置�?2MHz�?
 *         3. 外设初始化（GPIO、USART1、USART2、NVIC�?
 *         4. LoRa 模块初始化和参数配置
 *         5. 默认进入接收模式
 *         5. 主循环：LED 心跳、模式处�?
 */
static void processDtuFrame(char *pBuf) {
  printf("[DTU RX] %s\r\n", pBuf);
  /* JSON and module replies are separate frames; a JSON payload containing
   * words such as PING or config,csq must never suppress command execution. */
  if (pBuf[0] == '{') { parseDtuCommand(pBuf); return; }
      /* platform PING heartbeat: "PING1200" -> window=1200s.
       * presence of this frame proves the full server->4G->DTU link is up */
      {
        char *pPing = strncmp(pBuf, "PING", 4) == 0 ? pBuf : NULL;

        if (pPing) {
          uint32_t pingSec;
          if (!centen_parse_uint(pPing + 4, 60, 86400, &pingSec)) return;

          if (pingSec >= 60 && pingSec <= 86400)
            dtuPingWinMs = (uint32_t)pingSec * 1000u;
          dtuPingGot = 1;
          dtuLastPingTick = HAL_GetTick();
          printf("[DTU PING] ok w=%lus\r\n",
                 (unsigned long)(dtuPingWinMs / 1000u));
        }
      }

      /* 解析 CSQ 信号质量: config,csq,ok,<value> */
      char *pCsq = strstr(pBuf, "config,csq,ok,");
      if (pCsq) {
        int csqVal = 0;
        if (sscanf(pCsq, "config,csq,ok,%d", &csqVal) == 1 && ((csqVal >= 0 && csqVal <= 31) || csqVal == 99)) {
          dtuCsqRssi = csqVal; // 保存 CSQ 值供对时上报使用
          dtuCsqAnswerTick = HAL_GetTick(); // CSQ 有应�?= DTU 活着
        }
      }

      /* 解析网络时间: config,nettime,ok,�?�?�?�?�?�?毫秒 */
      char *pNet = strstr(pBuf, "config,nettime,ok,");
      if (pNet) {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
        if (sscanf(pNet, "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", &y, &mo, &d,
                   &h, &mi, &s, &ms) >= 6 &&
            dateTimeToTimestamp(y, mo, d, h, mi, s) != 0) {
          /* 保存时间戳供 LoRa 上报使用 */
          dtuTimestamp = dateTimeToTimestamp(y, mo, d, h, mi, s);
          dtuSyncTick = HAL_GetTick(); // 记录对时时刻
          dtuTimeValid = 1;
          dtuQueryAnswered = 1;
          dtuQueryPending = 0; // 标记网络时间已获�?
          dtuNetTimeFailCount = 0;
          printf("[DTU TIME] ts=%lu\r\n", (unsigned long)dtuTimestamp);  /* 重置连续失败计数�?*/

          /* 上报延迟 10 秒：拿到 nettime �?DTU 已连上服务器——重启后
           * DTU 注网/建立平台连接有窗口期，立即入队的 JSON 会被挤丢
           * （实测对时帧丢失、配置帧成功）。到期后由主循环统一入队 */
          syncReportDue = HAL_GetTick() + 10000;
          syncReportPending = 1;
        }
      }

      /* 解析 SIM �?ICCID: config,iccid,ok,<20位号�? —�?开机查询一次即�?*/
      char *pIccid = strstr(pBuf, "config,iccid,ok,");
      if (pIccid) {
        char iccid[24] = {0};
        if (sscanf(pIccid, "config,iccid,ok,%22s", iccid) == 1 && iccid[0]) {
          strncpy(dtuIccidStr, iccid, sizeof(dtuIccidStr) - 1);
          dtuIccidStr[sizeof(dtuIccidStr) - 1] = '\0';
          dtuIccidGot = 1;
        }
      }

      /* 解析 SIM �?IMSI: config,imsi,ok,<15�? —�?开机查询一次即�?*/
      char *pImsi = strstr(pBuf, "config,imsi,ok,");
      if (pImsi) {
        char imsi[24] = {0};
        if (sscanf(pImsi, "config,imsi,ok,%22s", imsi) == 1 && imsi[0]) {
          strncpy(dtuImsiStr, imsi, sizeof(dtuImsiStr) - 1);
          dtuImsiStr[sizeof(dtuImsiStr) - 1] = '\0';
          dtuImsiGot = 1;
        }
      }

      /* 都没匹配到，原样打印明文�?
       * config,get, 开头的�?DTU 对配置命令的回显（如 config,get,iccid），
       * 不是云端 JSON 指令，不能进 parseDtuCommand —�?iccid/imsi 的真�?
       * 应答在后续帧里由上面�?pIccid/pImsi 分支处理 */
      if (!pCsq && !pNet && !pIccid && !pImsi && !strstr(pBuf, "config,get,") &&
          !strstr(pBuf, "PING")) {
        /* 收到 DTU 下发数据：先把当前生效的工作时间打出来，
         * 放在 parseDtuCommand() 之前，方便对�?指令执行�?/ 执行�? */

        /* JSON 解析 DTU 下发的指�?*/
        parseDtuCommand(pBuf);
      }


}

static void processDtuInput(void) {
  uint8_t byte;
  uint32_t count = 0, primask;
  if (centenDtuRxRecover) {
    primask = __get_PRIMASK(); __disable_irq();
    HAL_UART_AbortReceive(&huart3);
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    centenDtuRing.tail = centenDtuRing.head;
    memset(&dtuFrame, 0, sizeof(dtuFrame));
    centenDtuRxRecover = 0;
    centen_dtu_receive_start(&huart3);
    __set_PRIMASK(primask);
  }
  if (dtuOverflowSeen != centenDtuRing.dropped) {
    primask = __get_PRIMASK(); __disable_irq();
    dtuOverflowSeen = centenDtuRing.dropped;
    centenDtuRing.tail = centenDtuRing.head;
    memset(&dtuFrame, 0, sizeof(dtuFrame));
    dtuFrame.dropping = 1;
    __set_PRIMASK(primask);
  }
  while (count++ < CENTEN_RX_RING_SIZE && centen_ring_pop(&centenDtuRing, &byte)) {
    if (centen_frame_feed(&dtuFrame, byte)) processDtuFrame(dtuFrame.data);
    if (centenDtuRxRecover) break;
  }
  if (centenDtuRing.head == centenDtuRing.tail && HAL_GetTick() - centenDtuLastRxTick > 500u &&
      centen_frame_idle(&dtuFrame)) processDtuFrame(dtuFrame.data);
}

static void processDtuQueries(void) {
  uint32_t now = HAL_GetTick();
  const char *cmd = NULL;
  if (radioTxActive) return;
  if (dtuQueryStage && now - dtuQueryTick >= 300u) {
    if (dtuQueryStage == 1) {
      cmd = "config,get,nettime\r\n";
      dtuQueryPending = 1; dtuQueryAnswered = 0;
    } else if (dtuQueryStage == 2 && !dtuImsiGot) cmd = "config,get,imsi\r\n";
    dtuQueryStage = 0;
    dtuQueryTick = now;
  } else if (dtuQueryPending && now - dtuQueryTick >= DTU_QUERY_WAIT_MS) {
    dtuQueryPending = 0;
    if (!dtuQueryAnswered && !dtuTimeValid && now - dtuRecoveryStart >= DTU_RECOVERY_GRACE_MS) {
      if (++dtuNetTimeFailCount >= 5) { dtuHardwareReset(); return; }
    }
  } else if (!dtuQueryStage && !dtuQueryPending &&
      now - dtuCsqTick >= (dtuTimeValid ? getConfigIntervalMs() : 10000u)) {
    dtuCsqTick = now; dtuQueryTick = now; dtuQueryStage = 1;
    cmd = "config,get,csq\r\n";
  } else if (!dtuQueryStage && !dtuQueryPending && dtuTimeValid &&
      (!dtuIccidGot || !dtuImsiGot) && now - dtuIccidTick >= 5000u) {
    dtuIccidTick = now; dtuQueryTick = now;
    if (!dtuIccidGot) { cmd = "config,get,iccid\r\n"; dtuQueryStage = 2; }
    else cmd = "config,get,imsi\r\n";
  }
  if (cmd) {
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
    printf("[DTU QUERY] %s", cmd);
    if (status != HAL_OK) {
      dtuTxFailures++;
      dtuQueryStage = dtuQueryPending = 0;
      printf("[DTU QUERY FAIL] status=%u\r\n", (unsigned int)status);
    }
  }
  if (dtuTimeValid && dtuCsqAnswerTick && now - dtuCsqAnswerTick > 65UL * 60000UL) {
    dtuRstByCsq = 1; dtuHardwareReset();
  }
}

int main(void) {
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/

  /* 复位所有外设，初始�?Flash 接口�?SysTick 定时�?*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 配置系统时钟 */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有配置的外设 */
  MX_GPIO_Init();        // GPIO 初始化（LED、按键、LoRa CSN/RST/IRQ�?
  MX_SPI1_Init();        // SPI1 初始化（LoRa 硬件 SPI�?
  MX_USART1_UART_Init(); // USART1 初始化（调试串口�?15200 波特率）
  MX_USART3_UART_Init(); // USART3 初始化（DTU 通信�?15200 波特率）

  /* 初始�?NVIC 中断控制器（LoRa IRQ 使用 PA2 EXTI�?*/
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */

  // 打印启动信息
  printf_logo(); // 显示工程信息 Logo

  /* Device name: default v0-100. A bound name (bindDeviceId command) is
   * loaded from Flash right after the config (printed there). */

  /* �?Flash 加载配置 */
  loadConfigFromFlash();
  loadNameFromFlash();   /* override default if a bindDeviceId was saved */

  /* 开机打印一次工作时间解码结果，供核对字典顺序是否正�?*/

  /* 测试公共模块 */


  HAL_Delay(1); // 短暂延时，等待串口输出完�?

  /* LoRa PAN3029 模块初始�?*/

  /* 确保 CSN �?RST 引脚为高电平（LoRa 模块空闲状态） */
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET); // CSN 拉高
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET); // RST 拉高
  HAL_Delay(100); // 等待模块电源稳定

  // 初始�?LoRa 射频模块
  if (!lora_rf_wake()) {
    loraSwOn = 0;
    radioRecoverPending = 1;
    radioRecoverTick = HAL_GetTick();
    printf("[LoRa] n/a\r\n");
  }
  dtuCsqTick = HAL_GetTick();
  dtuRecoveryStart = dtuPingGraceStart = dtuCsqTick;

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1) {
    processDtuInput();
    serviceRadio();
    getCurrentTimestamp();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* [调试] 每秒打印一�?STM32 实时时间 */


    /* PB15 (LED1) 接收指示：点�?00ms后自动熄�?*/
    if (led1On && HAL_GetTick() - led1OnTick >= 500) {
      led1On = 0;
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }

    /* 上报到期：nettime 成功后延�?10 秒，DTU 平台连接已稳定，
     * 统一入队 对时�?+ rst_dtu 告知（如有）+ 开机配置帧（如有） */
    if (syncReportPending && dtuTimeValid && centen_due(HAL_GetTick(), syncReportDue)) {
      syncReportPending = 0;
      sendTimeSyncToDtu();
      /* CSQ 死机复位�?DTU：补发一条告知帧（只发一次）
       * 格式: 8|deviceName|rst_dtu —�?服务器据此知道中继曾自动恢复�?*/
      if (dtuRstByCsq) {
        char rstStr[48];
        snprintf(rstStr, sizeof(rstStr), "8|%s|rst_dtu", (char *)deviceName);
        sendToDtu(rstStr, dtuCsqRssi, 0);
        dtuRstByCsq = 0;
      }
      /* PING timeout RST'd the DTU: report once the link recovered.
       * frame: 8|dev|DTURST|PING<windowSec>| �� server knows the outage
       * happened and the heartbeat watchdog did its job */
      if (dtuRstByPing) {
        char rstStr[48];
        snprintf(rstStr, sizeof(rstStr), "8|%s|DTURST|PING%lu|",
                 (char *)deviceName, (unsigned long)(dtuPingWinMs / 1000u));
        sendToDtu(rstStr, dtuCsqRssi, 0);
        dtuRstByPing = 0;
      }
      // 开机后首次获取到网络时间，上报一次配置信息（仅上报一次）
      if (!configBootSent) {
        configBootSent = 1;
        sendConfigToDtu();
      }
    }

    /* 过期缓存指令清理（每 30 秒扫一遍，time 时效到期不再下发�?*/
    /* PING heartbeat timeout: judged only after grace period (boot / post-RST
     * registration). No PING within window -> link broken -> RST DTU once,
     * then re-arm grace and report the RST once the link recovers */
    {
      uint32_t nowT = HAL_GetTick();
      uint8_t  pingRst = 0;

      if (nowT - dtuPingGraceStart < 180000u) {
        /* grace: DTU still registering after boot/reset (3 min) */
      } else if (!dtuPingGot && nowT - dtuPingGraceStart > dtuPingWinMs) {
        pingRst = 1;                    /* zero PING since boot */
      } else if (dtuPingGot && nowT - dtuLastPingTick > dtuPingWinMs) {
        pingRst = 1;                    /* heartbeat lost */
      }
      if (pingRst) {
        printf("[DTU PING] lost, RST dtu\r\n");
        dtuRstByPing = 1;
        dtuHardwareReset();
        dtuPingGot = 0;
        dtuPingGraceStart = HAL_GetTick(); /* re-arm grace for registration */
      }
    }

    cleanupExpiredPendingCmds();

    /* DTU 发送队列处理：�?2 秒从队列取出一条消息发�?*/
    processDtuQueue();

    /* 工作区间自动开关射频（每秒检测一次，只在进出区间的那一刻动作）
     * 边沿触发而非全程强制：区间内手动 lorasw|0 关掉后会一直保持关闭，
     * 直到区间结束再进入才自动重开，手动关在区间内因此仍然有效
     * 工作区间优先级高�?lorasw 指令：区间外一律休�?*/
    if (HAL_GetTick() - workWinCheckTick >= 1000) {
      uint8_t inWin = isInWorkWindow();

      workWinCheckTick = HAL_GetTick();

      if (inWin != prevInWorkWindow) {
        if (inWin) {
          printf("[WORK] on\r\n");
          if (!loraSwOn)
            lora_rf_wake();
        } else {
          printf("[WORK] off\r\n");
          if (loraSwOn)
            lora_rf_sleep();
        }
        prevInWorkWindow = inWin;
      }
    }

    /* 根据当前工作模式执行相应处理 */
    switch (workMode) {
    case MODE_RX: // 接收模式
      OnSlave();  // 调用接收处理函数
      break;

    default: // 空闲模式，不做任何处�?
      break;
    }

    processDtuInput();
    processDtuQueries();
    serviceRadio();

    HAL_Delay(1); // 短延�?
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void) {
  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */
}

/**
 * @brief  GPIO 初始化函�?
 * @note   配置说明�?
 *         1. LED 指示灯（本板仅有 1 个）�?
 *            - LED1 (PB15) - 输出，推挽，低速，默认�?
 *                            收到 LoRa 数据时点亮，500ms 后由主循环熄灭；
 *                            LoRa 初始化失败时快速闪烁报�?
 *         2. 按键输入�?
 *            - 本板无按键（只有复位键）
 *            - �?K1(PB4)/K2(PB5) 的引脚定义、GPIO 配置、NVIC 使能�?
 *              EXTI4/EXTI9_5 中断服务函数均已删除
 *         3. LoRa SPI 引脚（软件模�?SPI）：
 *            - SCK (PA5) - 输出，推挽，高�?
 *            - MOSI (PA7) - 输出，推挽，高�?
 *            - MISO (PA6) - 输入，无上下�?
 *         4. LoRa 控制引脚�?
 *            - CSN (PB0) - 输出，推挽，高速，默认高电平（未选中�?
 *            - RST (PA1) - 输出，推挽，高速，默认高电平（不复位）
 *         5. LoRa 中断引脚�?
 *            - IRQ (PA2) - 外部中断，上升沿触发，下�?
 *         6. DTU 复位引脚�?
 *            - DTU_RST (PA3) - 输出，推挽，默认高电平（不复位）�?
 *              拉低 100ms 脉冲复位 DTU 模块（debug|2 指令/注网失败恢复�?
 * @param  None
 * @retval None
 */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* 使能 GPIO 端口时钟 */
  __HAL_RCC_GPIOC_CLK_ENABLE(); // GPIOC 时钟
  __HAL_RCC_GPIOD_CLK_ENABLE(); // GPIOD 时钟
  __HAL_RCC_GPIOA_CLK_ENABLE(); // GPIOA 时钟
  __HAL_RCC_GPIOB_CLK_ENABLE(); // GPIOB 时钟

  /* 配置 GPIO 输出引脚初始电平 */
  HAL_GPIO_WritePin(GPIOB, LED1_Pin, GPIO_PIN_RESET); // LED1 默认�?

  /* 配置 LED1 引脚 (PB15) —�?本板唯一�?LED */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;          // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // 低�?
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 按键 K1(PB4)/K2(PB5) 本板已无此引脚，引脚配置�?EXTI 使能整体删除 */

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* 配置 LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin = LORA_SCK_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;       // 复用推挽输出（硬�?SPI�?
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高�?
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin = LORA_MISO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 配置 LoRa CSN 片选引�?(PB0) - 默认高电平（未选中�?*/
  GPIO_InitStruct.Pin = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高�?
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET); // CSN 拉高

  /* 配置 LoRa RST 复位引脚 (PA1) - 默认高电平（不复位） */
  GPIO_InitStruct.Pin = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高�?
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET); // RST 拉高

  /* 配置 DTU RST 复位引脚 (PA3) - 高电平有效复位！
   * 实测确认：平时必须拉低（不复位），拉�?= 复位脉冲�?
   * （之前默认高电平曾把 DTU 摁死在复位状态导致无法联网） */
  GPIO_InitStruct.Pin = DTU_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DTU_RST_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET); // 平时低（不复位）

  /* 配置 LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; // 外部中断，上升沿触发
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;       // 下拉（默认低电平�?
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
 * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI�?
 * @note   配置 SPI1 外设�?
 *         - 主机模式，全双工�?-bit 数据
 *         - CPOL=0, CPHA=0（空闲时 SCK 低电平，第一边沿采样�?
 *         - MSB 优先，软�?NSS，波特率分频 16�?2MHz/16 = 4.5MHz�?
 * @param  None
 * @retval None
 */
static void MX_SPI1_Init(void) {
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW; // CPOL=0
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;     // CPHA=0
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; // 72/16 = 4.5MHz
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief  NVIC 中断控制器初始化函数（LoRa IRQ�?
 * @note   配置 LoRa 模块�?IRQ 引脚 (PA2) 对应的外部中�?EXTI2
 *         - 中断优先�? 抢占优先�?0，子优先�?0（最高优先级�?
 *         - 触发方式: 上升沿触发（�?GPIO 初始化中配置�?
 * @retval None
 */
static void MX_NVIC_Init(void) {
  /* EXTI2 中断用于 LoRa IRQ (PA2) */
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0); // 设置最高优先级
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);         // 使能 EXTI2 中断

  /* USART3 中断用于 DTU 数据接收 */
  HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/* USER CODE BEGIN 4 */

/**
 * @brief  GPIO 外部中断回调函数
 * @note   �?EXTI 中断触发时，HAL 库会调用此函�?
 *         根据中断引脚判断中断源，执行相应处理
 * @param  GPIO_Pin: 触发中断的引脚号
 * @retval None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  /* LoRa IRQ 中断: PA2 -> 调用射频中断处理函数 */
  if (GPIO_Pin == GPIO_PIN_2) {
    radioIrqPending = 1; // 处理 LoRa 模块的中断事�?
  }

  /* 本板无按键，K1(PB4)/K2(PB5) 已从工程彻底删除
   * （引脚定义、GPIO 配置、NVIC 使能、EXTI4/EXTI9_5 中断服务函数全部移除�?
   * 若以后新板要加按键，需三处一起补回：
   *   1) main.h �?K1_Pin/K2_Pin 定义
   *   2) MX_GPIO_Init �?IT_FALLING + PULLUP 配置�?NVIC 使能
   *   3) stm32f1xx_it.c �?EXTI4_IRQHandler / EXTI9_5_IRQHandler
   * 且射频操作必须放在主循环，绝不能在中断里调用（按�?EXTI 优先�?0�?
   * SysTick 优先�?15，中断里�?HAL_Delay 会整机卡死） */
}

/* USER CODE END 4 */

/**
 * @brief USART3 Initialization Function (DTU 通信)
 * @param None
 * @retval None
 */
static void MX_USART3_UART_Init(void) {
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK) {
    Error_Handler();
  }

  /* 启动中断接收，每个字节触发一次回�?*/
  centen_dtu_receive_start(&huart3);
}

/**
 * @brief  USART3 接收完成回调（HAL 库弱函数重写�?
 * @note   每收�?1 字节触发一次，记录时间戳并续接缓冲�?
 */
/**
 * @brief  错误处理函数
 * @note   当系统发生严重错误时调用此函�?
 *         关闭所有中断，进入死循�?
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq(); // 关闭所有中�?
  while (1) {
    // 死循环，等待硬件复位
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  断言失败回调函数
 * @note   �?assert_param 检查失败时调用此函�?
 *         可以在此处添加调试信息，输出文件名和行号
 * @param  file: 指向源文件名的指�?
 * @param  line: 断言失败的源代码行号
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* 可以在此处添加调试输出，例如�?
   * printf("Wrong parameters value: file %s on line %d\r\n", file, line);
   */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
