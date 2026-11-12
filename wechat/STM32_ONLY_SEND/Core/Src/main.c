/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 定时上报节点 - STM32F103C8T6
  *                   移植自 PAN3029_F030 上报逻辑
  * @note           功能说明：
  *                 1. TX 模式：每隔一段时间上报一帧 MSG_TYPE_TIME|设备名|当天秒数|电量|计数|
                       （固定发时间帧；GPS 坐标由 upgps 指令单独触发上报）
  *                 2. 发送后开启 3 秒 RX 窗口，解析对时消息（MSG_TYPE_SYN_TIME）
  *                 3. 调试输出：通过 USART1 输出系统状态
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "pan3029_rf.h"
#include "pan3029_port.h"
#include "radio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LoRa Only Send"
#define DEMO_VER     "V1.2"
#define EXAMPLE_DATE "2026/10/18"

#define TX_LEN       64
#define RX_LEN       64

/* 消息类型定义（与 PAN3029_F030 对齐） */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_WARN     8   /* 告警消息（预留，当前未使用；类型 8 已给 READINFO） */
#define MSG_TYPE_READINFO 8   /* 设备信息上报（readinfo 指令回复）
                               * 帧格式: 8|设备名|设备数量|发射功率(dBm)|保留(当前填计数)|
                               * @note 与 MSG_TYPE_WARN 同值 8：告警消息从未实现过，
                               *       若将来要做告警，必须换一个类型号 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* 设备信息 */
#define DEFAULT_DEVICE_NAME  "v3-1"
char deviceName[32] = DEFAULT_DEVICE_NAME;  /* 可变设备名，从 Flash 加载 */
#define BATTERY_VAL  99   /* 电量默认值（%）：ADC 取到值前的兜底，上电即被刷新 */

/* 配置字符串（持久化到 Flash，可通过指令修改） */
#define DEFAULT_CONFIG_STR  "1,0M,3t,6"
char configStr[64] = DEFAULT_CONFIG_STR;

/* 农场中心坐标（"lat,lng"，空=未设置，由 C 指令下发并持久化） */
char centerStr[64] = "";

/* Flash 存储地址（STM32F103C8T6 64KB，每项独占一页，擦除互不干扰） */
#define DEVNAME_FLASH_ADDR  0x0800F800  /* 设备名存储（倒数第二页） */
#define DEVNAME_MAGIC       0xA5A55A5B  /* 修改魔术字使旧 Flash 数据失效 */
#define CONFIG_FLASH_ADDR   0x0800F400  /* 配置字符串存储（倒数第三页） */
#define CONFIG_MAGIC        0x5A5AA5A6  /* 修改魔术字使旧 Flash 数据失效 */
#define TXPWR_FLASH_ADDR    0x0800F000  /* 发射功率存储（倒数第四页） */
#define TXPWR_MAGIC         0xA5A55A5C  /* 功率魔术字 */
#define CENTER_FLASH_ADDR   0x0800EC00  /* 农场中心坐标存储（倒数第五页） */
#define CENTER_MAGIC        0xA5A55A5D  /* 中心坐标魔术字 */
#define DEVCNT_FLASH_ADDR   0x0800E800  /* 子设备总台数存储（倒数第六页） */
#define DRIFT_FLASH_ADDR    0x0800E400  /* 晶振补偿值存储（倒数第七页） */
#define DEVCNT_MAGIC        0xA5A55A5E  /* 设备台数魔术字 */
#define DRIFT_MAGIC         0xA5A55A5F  /* 晶振补偿值魔术字 */

/* 上报间隔：由 configStr 第 1 段决定（分钟），如 "30,0M,3t,6" = 30 分钟
 * configStr 四段含义:
 *   第 1 段 "1"  = 工作期间的上报周期(分钟)，范围 1~60
 *   第 2 段 "0M" = 工作时间段起始，base62 双字符编码 → idx 22
 *   第 3 段 "3t" = GPS 自动上报时间段，base62 双字符编码 → idx 241
 *   第 4 段 "6"  = 保留 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;    /* LoRa PAN3029 */
UART_HandleTypeDef huart1;  /* 调试打印 */
UART_HandleTypeDef huart3;  /* GPS 模块 USART3 (PB11 RX, 9600, 仅接收) */

/* USER CODE BEGIN PV */

/* 时间同步变量（时间基准 = RTC 秒计数器，STOP 深度休眠期间继续走） */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间戳 */
static uint32_t syncedRtcSec = 0;     /* 对时那一刻的 RTC 计数（秒） */
static uint8_t  timeSynced = 0;       /* 是否已同步时间 */
static uint32_t anchorRtcSec = 0;     /* 首次对时的 RTC 基准（用于晶振偏差计算） */
static uint16_t gpsContTotalMin = 0;   /* upgps 指令带的分钟数参数：
                                        *   0   = 单次搜星上报（现有逻辑）
                                        * 1~60  = 连续定位模式：这么长时间内不休眠，
                                        *         每 GPS_CONT_INTERVAL_SEC 秒上报一次坐标
                                        * 用完即清零，故失败重试时只会走单次逻辑 */
static uint32_t anchorTimestamp = 0;  /* 首次对时的 Unix 时间戳基准 */
/* --- 晶振偏差补偿（收敛后才启用）---
 * 每次对时测出偏差(秒/小时, 负=本地慢)；连续两次测量相差 ≤ DRIFT_CONVERGE_TOL
 * 才视为收敛并启用补偿：休眠闹钟按偏差换算（准点），本机时间也回到真实轴。
 * 未收敛时两值均为 0 → 所有换算退化成原行为，零风险 */
static int32_t  rtcDriftPerHour = 0;  /* 当前采用的晶振偏差（秒/小时） */
static uint8_t  driftConverged = 0;   /* 1=偏差已收敛, 补偿生效 */
static int32_t  lastDriftSample = 0;  /* 上一次对时测得的偏差（收敛判定用） */
static uint8_t  configNeedReport = 1;  /* 配置需要上报（开机=1，收到指令更新=1，上报后=0） */
static uint8_t  centerNeedReport = 0;  /* 中心坐标需要上报（收到 C 指令=1，上报后=0） */
static uint8_t  readInfoNeedReport = 0;/* 设备信息需要上报（收到 readinfo 指令=1，上报后=0） */
static uint8_t  gpsWaitReport = 0;     /* 收到 upgps 指令=1，处理完自动清零 */
static uint8_t  gpsNeedRetry = 0;      /* 上一轮搜星失败(已上报 0.0,0.0)=1
                                        * → 本轮跳过定时上报，直接重试搜星上报坐标；
                                        *   成功拿到坐标后自动清零，恢复定时上报
                                        * @note  RAM 在 deepsleep 期间保持，故跨休眠有效；
                                        *        但真正掉电重启会丢失(回到0)，这是可接受的 */
static uint8_t  upgpsRetryOneShot = 0; /* UPGPS 跟踪失败结束后的一次性重试许可：
                                        * 只允许下轮做一次类型 1 搜星上报，
                                        * 这次仍无坐标就清零，之后不再重试
                                        * （60 分钟穷尽+1 次重试都失败=环境真无信号） */
static uint32_t lastLoRaRxTs = 0;      /* 最近一次收到 LoRa 数据的时刻(绝对时间戳)
                                        * 0=尚未初始化(开机后首次检查时取当前值)。
                                        * 链路沉默看门狗用：工作时间内 2 小时没收到
                                        * 任何 LoRa 信息 → NVIC_SystemReset 真复位 */
/* 提前搜星结果（需要 GPS 的上报轮，唤醒后先搜星再等到点上报，保证时间点不后移） */
static uint8_t  gpsPreSearched = 0;    /* 1=本轮已提前搜过星，OnMaster 直接取结果上报 */
static uint8_t  gpsPreFixOk    = 0;    /* 预搜结果：1=拿到有效坐标 */
static uint32_t gpsPreSec      = 0;    /* 预搜耗时（秒），上报帧第 4 段用 */
static uint32_t gpsPreBytes    = 0;    /* 预搜期间 GPS 串口字节数，无定位时上报帧第 4 段用 */
#define TX_COUNT_MAX   999             /* 上报帧计数上限：超过后回绕到 1
                                        * 好处是帧里这一字段恒为 1~3 位数字，
                                        * 帧长稳定，也不用担心 uint32 溢出(约 49 天后) */

static uint32_t tx_count = 0;          /* 上报帧计数（每发一帧 LoRa +1，见 txCountNext()）
                                        * 放在文件作用域：配置帧/中心坐标帧/定位帧
                                        * 也都要带这个计数，便于云端判重和算丢包 */
static uint32_t nextTxTimestamp = 0;   /* 下次上报的绝对时间戳（秒） */
/* 开机前 3 轮快速上报配额：配置周期 > 1 分钟时，前 3 轮先用 1 分钟
 * （让设备尽快对时、收配置、进入正常时隙节奏），3 轮后恢复配置周期。
 * 每完成一轮 OnMaster 递减一次；掉电重启重新计 3 轮 */
static uint8_t  fastStartRounds = 3;
static uint8_t  currentTxPower = DEFAULT_PWR;  /* 当前发射功率（从 Flash 加载或指令修改） */
static uint8_t  deviceCount    = 30;  /* 分时隙上报的子设备总台数（默认 30，可由
                                       * devicecount 指令下发并持久化到 Flash） */
static uint8_t  loraInitialized = 0;  /* LoRa 是否已初始化（首次 TX 前才初始化） */
static uint8_t  cachedBatteryPercent = BATTERY_VAL;  /* 缓存电量百分比（0~99）
                                        * 每次 LoRa 唤醒时读一次 ADC 刷新，
                                        * 上报帧里带的就是这个值 */

/* --- 电池电量检测（PA0 / ADC1_CH0）---
 * @note  硬件：电池 → 100K → PA0 → 100K → GND，即 1:1 分压
 *        故 V_bat = ADC电压 × 2，电池满电 4.2V 时分压点约 2.1V（未超 3.3V 量程）
 *        用寄存器方式操作 ADC1，不依赖 stm32f1xx_hal_adc.c
 */

/* --- GPS 模块（USART3, PB11 RX, 9600）---
 * @note  本版本不做 GPS 电源三级管/复位控制，模块常供电，直接接收 NMEA
 *        设计要点：中断里只做"塞入环形缓冲"这个最轻的动作，
 *        解析和打印全部放在主循环，避免在 ISR 里跑串处理和 printf
 */
#define GPS_RING_LEN   512   /* 环形缓冲：主循环被 LoRa 收发阻塞时先攒着 */
#define GPS_LINE_LEN   128   /* 单条 NMEA 语句上限 */
#define GPS_FIX_TTL_MS 60000 /* 定位有效期(ms)：超过这么久没刷新就算失锁 */
#define GPS_WAIT_TIMEOUT_MS 240000 /* upgps 搜星上限 = 4 分钟；超时则上报 0.0,0.0
                                    * @note 冷启动首次定位最坏要 12.5 分钟，
                                    *       天线条件差时 3 分钟不够，先放宽到 4 分钟实测 */
#define GPS_WAIT_MIN_MS     10000  /* upgps 最短搜星时间 = 10 秒
                                    * @note 收到指令后不能马上发 LoRa：中继此时还没准备好接收，
                                    *       发太快大概率丢帧。即使立刻就有坐标，也要至少等够这 10 秒 */
/* 连续定位模式：每一轮**最少**占用的时间（搜星 + 等待）
 * 跟踪上报间隔 = 设置的搜星最大时间(GPS_WAIT_TIMEOUT_MS)，但下限 3 分钟：
 *   - 搜星上限调大/调小时，跟踪间隔自动跟随（间隔与单轮最坏耗时一致，
 *     搜星慢的轮本轮时长自然以搜星耗时为准，不会叠加催下一轮）；
 *   - 再设 180 秒下限，防止把搜星上限改小后跟踪上报过密。
 * 跟踪期间 GPS 一直保持供电、不进休眠；类型 5 上报不开接收窗口 */
#define GPS_CONT_INTERVAL_SEC \
    (((GPS_WAIT_TIMEOUT_MS) / 1000) > 180 ? ((GPS_WAIT_TIMEOUT_MS) / 1000) : 180)
#define GPS_CONT_MAX_MIN      60   /* upgps 分钟数参数上限 = 60 分钟 */
#define GPS_CONT_MAX_SEC      ((uint32_t)GPS_CONT_MAX_MIN * 60u)   /* 60 分钟的秒数：
   * 跟踪目标就是定位成功 —— 一直拿不到有效坐标时，跟踪期限自动保底延长
   * 到这里（期间每轮照常上报 0.0,0.0），拿到坐标后恢复参数设定的时长 */
/* 需要搜星的上报轮：提前唤醒量 = 搜星上限 + 5 秒（LoRa 初始化倒计时）
 * 醒来先搜星，搜到就把 GPS 断电再睡到上报点前 5 秒，到点准点发帧；
 * 搜不到也到点发 "0.0,0.0|串口字节数|"，保证上报时间点不后移 */
#define GPS_PRESEARCH_LEAD_SEC ((GPS_WAIT_TIMEOUT_MS / 1000) + 5)
#define GPS_PWR_ON_DELAY_MS  1000  /* PB15 上电后等待模块输出 NMEA 的时间(ms)
                                    * @note 电源冷启动（不用 WAKE_UP 休眠模式），
                                    *       比Standby热唤醒慢，给足 1 秒 */

/* GPS 电源控制总开关 (PB15): 1=由 MCU 控制电源通断, 0=不动电平（核对阶段）
 * 置 1 时的行为（当前生效，极性: 低=上电, 高=断电）:
 *   - 上电 PB15 默认高 → GPS 断电（完全关机，0 电流）
 *   - 搜星前 gpsPowerOn() 置低 → 上电开机，搜完 gpsPowerOff() 置高 → 断电
 *   - 深度休眠等待期模块始终断电，不耗任何电流
 * 置 0 时为核对模式：PB15 高阻、不翻转，模块状态由外部电源决定
 */
#define GPS_PWR_CTRL_EN  1

/* 链路沉默看门狗：工作时间内连续这么久没收到任何 LoRa 信息 → 真复位自恢复
 * （2 小时。跨 STOP 深度休眠连续计时——用 RTC 时间戳，休眠机制完全不变；
 *   连续定位跟踪期间不检查，避免打断主动任务） */
#define LINK_SILENCE_REBOOT_SEC  (2u * 3600u)

/* 晶振偏差收敛阈值（秒/小时）：连续两次对时测得的偏差相差 ≤ 此值视为收敛。
 * 30 分钟对时区间内 1 秒的测量误差 ≈ 12 秒/小时，取 15 留裕量 */
#define DRIFT_CONVERGE_TOL      15
/* 偏差样本剔除上限（秒/小时）：LSI 晶振最差约 ±5%（±180秒/h），
 * 超过 ±300 视为中继自身授时错误（中继时间跳变导致），样本直接丢弃——
 * 不参与收敛、不污染补偿值，收敛结果只由可信样本决定 */
#define DRIFT_SAMPLE_MAX        300



typedef struct {
  uint8_t  valid;   /* 已定位 */
  int32_t  lat;     /* 纬度 ×1e5（北纬为正），如 26.52958 → 2652958 */
  int32_t  lng;     /* 经度 ×1e5（东经为正） */
  uint8_t  sats;    /* 参与定位的卫星数 */
  /* 以下是 GPS 的 UTC 日期时间（来自 RMC），未收到过则全为 0
   * 用于搜星状态行的"年份是否可信"判断与时间显示 */
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
  uint16_t msec;
} GpsFix_t;

static const uint8_t gpsDaysInMonth[12] =   /* 平年每月天数（闰年 2 月单独修正） */
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static GpsFix_t     gpsFix;
static uint8_t      gpsFixValid = 0;   /* 最近一帧 GGA 是否有效定位 */
static uint8_t      gpsPowerState = 0; /* 当前 PB15 状态：1=上电搜星, 0=断电
                                        * （断电诊断用：断电时不该有串口数据） */
static uint32_t     gpsRxCount = 0;    /* 收到的字节数（诊断用） */
static uint32_t     gpsFixTick = 0;    /* 最近一次拿到有效定位的时刻（HAL_GetTick） */
static uint8_t      gpsPrintEn = 0;    /* GPS 打印总开关（1=打印, 0=静默）
                                        * 默认 0：正常启动/运行不打任何 GPS 日志；
                                        * 收到 upgps 后临时置 1，处理完自动恢复原值；
                                        * @note  只影响打印，不影响接收/解析/上报 */
static volatile uint8_t  gpsRing[GPS_RING_LEN];
static volatile uint16_t gpsWr = 0;    /* 写指针（ISR 更新） */
static volatile uint16_t gpsRd = 0;    /* 读指针（主循环更新） */
static volatile uint8_t  gpsByte = 0;  /* HAL_UART_Receive_IT 的单字节缓冲 */
static char     gpsLine[GPS_LINE_LEN];
static uint16_t gpsLineLen = 0;

extern struct RxDoneMsg RxDoneParams;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_NVIC_Init(void);
static void MX_ADC1_Init(void);

/* USER CODE BEGIN PFP */
static void printf_logo(void);
static uint32_t getTodaySecond(void);
static uint32_t getCurrentTimestamp(void);
static uint8_t isLeapYear(uint16_t year);
static void formatTimestamp(uint32_t ts, char *buf, uint8_t len);
static void reportPendingMessages(void);   /* 回发待上报的配置/中心坐标帧 */
static void OnMaster(void);
static void parse_rx_message(uint8_t *buf, uint8_t len);
static uint8_t cmdIsForMe(char *fields[]);   /* 指令目标校验(统一跳过打印) */
static void loadDeviceNameFromFlash(void);
static void saveDeviceNameToFlash(const char *name);
static void loadDriftFromFlash(void);
static void saveDriftIfChanged(int32_t d);
static void loadConfigFromFlash(void);
static void saveConfigToFlash(const char *str);
static void loadTxPowerFromFlash(void);
static void saveTxPowerToFlash(uint8_t power);
static void loadDeviceCountFromFlash(void);  /* 子设备总台数(默认30, devicecount 指令可改) */
static void saveDeviceCountToFlash(uint8_t count);
static void loadCenterFromFlash(void);
static void saveCenterToFlash(const char *str);
static uint8_t centerCoordValid(int32_t lat, int32_t lng);
static void    deg1e5ToStr(int32_t v, char *buf);
static uint8_t parseDeg1e5(const char *s, int32_t *out);
static void loraInitFirstTime(void);
static void sendConfigMessage(void);
static void sendCenterMessage(void);
static void sendReadInfoMessage(void);  /* 设备信息上报 8|设备|设备数量|发射功率|保留| */
static void sendGpsPositionMessage(uint8_t hasFix, uint8_t useGpsType,
                                   uint32_t searchSec, uint32_t rxBytes);
static uint8_t gpsSearchAndReport(uint8_t useGpsType);
static uint8_t gpsRunContinuous(uint16_t totalMin, uint8_t useGpsType);
static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs);
static void    gpsPrintSearchStatus(uint32_t elapsedMs);   /* 搜星状态行(每秒一行) */
static uint32_t getConfigIntervalSec(void);
static void    getConfigFieldStr(uint8_t idx, char *out, uint8_t outLen);
static int16_t twoCharToIndex(const char *s);    /* base62 双字符 → 索引(0~275) */
static uint8_t timeIdxToRange(int16_t idx, uint8_t *startHour, uint8_t *endHour);
static int16_t getConfigWorkStartIdx(void);      /* 工作时间段(第2段 "0M") */
static int16_t getConfigGpsEndIdx(void);         /* GPS 上报时间段(第3段 "3t") */
static uint8_t  getDeviceIndex(void);
static uint32_t getSlotOffsetSec(uint32_t intervalSec);
static void calcNextTxTime(void);
static void MX_USART3_UART_Init(void);
static void gpsTask(void);
static void gpsTrySyncFromSat(void);        /* 卫星授时兜底（本机未对时时用一次） */
static void parseNmeaLine(char *line);
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen);
static int32_t nmeaToDeg(const char *v, char dir);
static uint8_t gpsFixFresh(void);
static uint8_t isInWorkWindow(void);      /* 当前是否在工作时间段内(config 第2段) */
static void linkSilenceCheck(void);       /* 链路沉默看门狗(2小时无LoRa→真复位) */
static void    printTimeWindows(const char *tag);   /* 打印当前生效的两个时间段 */
static uint8_t isInGpsWorkWindow(void);   /* 当前是否在 GPS 工作时间段内(config 第3段) */
static uint8_t isInGpsWorkWindowAt(uint32_t ts);  /* 指定时刻是否在 GPS 时间段内 */
static uint8_t gpsNeededTimelyAt(uint32_t ts);    /* 该时刻是否"必须准点"的 GPS 轮(类型1) */
static void    gpsPreSearch(void);                /* 提前搜星（到点前必须完成） */
static void    openRxWindow3000(void);    /* 开 3 秒接收窗口并解析（可重复调用） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
static void gpsPowerOn(void);               /* GPS 唤醒：PB1(WAKE_UP) 置高 → Full on */
static void gpsPowerOff(void);              /* GPS 休眠：PB1(WAKE_UP) 置低 → Standby */
static uint16_t ADC1_Read(void);            /* ADC1 单次读取（12 位） */
static uint8_t  GetBatteryPercent(void);    /* ADC 值 → 电池电压 → 电量百分比 */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
  * @brief  printf 底层输出函数重定向（USART1）
  */
PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

/**
  * @brief  打印工程信息 Logo
  */
static void printf_logo(void)
{
  /* @note logo 已裁掉以省 Flash（MDK Lite 32KB 限制），保留空函数避免改调用处 */
}

/**
  * @brief  从 Flash 加载设备名
  * @note   如果 Flash 中有有效魔术字则加载，否则使用默认名
  */
static void loadDeviceNameFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)DEVNAME_FLASH_ADDR;
  if (magic == DEVNAME_MAGIC) {
    const char *p = (const char *)(DEVNAME_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(deviceName) - 1 && p[i] != '\0'; i++)
      deviceName[i] = p[i];
    deviceName[i] = '\0';
    printf("[Flash] 设备名已加载: %s\r\n", deviceName);
  } else {
    printf("[Flash] 无保存设备名, 使用默认: %s\r\n", deviceName);
  }
}

/**
  * @brief  保存设备名到 Flash
  * @param  name: 要保存的设备名字符串
  */
static void saveDeviceNameToFlash(const char *name)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;
  uint8_t i;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = DEVNAME_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  /* 写入魔术字 */
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DEVNAME_FLASH_ADDR, DEVNAME_MAGIC);

  /* 写入设备名字符串（按半字写入） */
  for (i = 0; i < strlen(name) + 1; i += 2) {
    uint16_t halfWord = name[i] | (name[i + 1] << 8);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, DEVNAME_FLASH_ADDR + 4 + i,
                      halfWord);
  }

  HAL_FLASH_Lock();
  printf("[Flash] 设备名已保存: %s\r\n", name);
}

/**
  * @brief  从 Flash 加载配置字符串
  */
static void loadConfigFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)CONFIG_FLASH_ADDR;
  if (magic == CONFIG_MAGIC) {
    const char *p = (const char *)(CONFIG_FLASH_ADDR + 4);
    uint8_t i;
    for (i = 0; i < sizeof(configStr) - 1 && p[i] != '\0'; i++)
      configStr[i] = p[i];
    configStr[i] = '\0';
    printf("[Flash] 配置已加载: %s\r\n", configStr);
  } else {
    printf("[Flash] 无保存配置, 使用默认: %s\r\n", configStr);
  }
}

/**
  * @brief  保存配置字符串到 Flash
  */
static void saveConfigToFlash(const char *str)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;
  uint8_t i;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = CONFIG_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_FLASH_ADDR, CONFIG_MAGIC);

  for (i = 0; i < strlen(str) + 1; i += 2) {
    uint16_t halfWord = str[i] | (str[i + 1] << 8);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, CONFIG_FLASH_ADDR + 4 + i,
                      halfWord);
  }

  HAL_FLASH_Lock();
  printf("[Flash] 配置已保存: %s\r\n", str);
}

/**
  * @brief  从 Flash 加载子设备总台数（devicecount 指令持久化的值）
  * @note   无保存 / 值超范围(1~60)时用默认 30
  */
static void loadDeviceCountFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)DEVCNT_FLASH_ADDR;
  if (magic == DEVCNT_MAGIC) {
    uint8_t saved = (uint8_t)(*(volatile uint32_t *)(DEVCNT_FLASH_ADDR + 4));
    if (saved >= 1 && saved <= 60) {
      deviceCount = saved;
      printf("[Flash] 设备台数已加载: %d 台\r\n", deviceCount);
    } else {
      printf("[Flash] 设备台数超范围(%d), 使用默认: %d\r\n", saved, deviceCount);
    }
  } else {
    printf("[Flash] 无保存设备台数, 使用默认: %d\r\n", deviceCount);
  }
}

/**
  * @brief  保存子设备总台数到 Flash
  */
static void saveDeviceCountToFlash(uint8_t count)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = DEVCNT_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DEVCNT_FLASH_ADDR, DEVCNT_MAGIC);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DEVCNT_FLASH_ADDR + 4, count);

  HAL_FLASH_Lock();
  printf("[Flash] 设备台数已保存: %d\r\n", count);
}

/**
  * @brief  从 Flash 加载收敛后的晶振补偿值
  * @note   真复位/断电重启后 RAM 清零 —— 从 Flash 恢复后补偿立即生效，
  *         不用重新等 3 次对时收敛；没有保存记录则走正常收敛流程
  */
static void loadDriftFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)DRIFT_FLASH_ADDR;

  if (magic == DRIFT_MAGIC) {
    int32_t d = (int32_t)(*(volatile uint32_t *)(DRIFT_FLASH_ADDR + 4));

    rtcDriftPerHour = d;
    lastDriftSample = d;
    driftConverged  = 1;
    printf("[Flash] 补偿已加载: %d秒/h\r\n", (int)d);
  }
}

/**
  * @brief  保存晶振补偿值到 Flash（变化 ≤10秒/h 不写，防页磨损）
  * @note   F1 页擦写寿命约 1 万次：若每次对时都写（48 次/天）100 天就磨穿。
  *         晶振特性稳定，收敛后采用值抖动 ±几秒/h，设 10秒/h 死区后
  *         实际写入频率极低（数天~数周一次），寿命以年计
  */
static void saveDriftIfChanged(int32_t d)
{
  static int32_t saved = 10000;   /* 初值必不等于真实偏差 → 首次必写 */
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  if (d > saved - 10 && d < saved + 10)
    return;

  saved = d;
  eraseInit.TypeErase  = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = DRIFT_FLASH_ADDR;
  eraseInit.NbPages    = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DRIFT_FLASH_ADDR, DRIFT_MAGIC);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DRIFT_FLASH_ADDR + 4, (uint32_t)d);
  HAL_FLASH_Lock();
  printf("[Flash] 补偿值已存: %d秒/h\r\n", (int)d);
}

/**
  * @brief  从 Flash 加载发射功率
  */
static void loadTxPowerFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)TXPWR_FLASH_ADDR;
  if (magic == TXPWR_MAGIC) {
    uint8_t saved = (uint8_t)(*(volatile uint32_t *)(TXPWR_FLASH_ADDR + 4));
    if (saved >= 4 && saved <= 20) {
      currentTxPower = saved;
      printf("[Flash] 发射功率已加载: %d dBm\r\n", currentTxPower);
    } else {
      printf("[Flash] 功率%d超范围, 默认%d\r\n",
             saved, DEFAULT_PWR);
    }
  } else {
    printf("[Flash] 无保存发射功率, 使用默认: %d dBm\r\n", DEFAULT_PWR);
  }
}

/**
  * @brief  保存发射功率到 Flash
  */
static void saveTxPowerToFlash(uint8_t power)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = TXPWR_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, TXPWR_FLASH_ADDR, TXPWR_MAGIC);
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, TXPWR_FLASH_ADDR + 4, (uint32_t)power);

  HAL_FLASH_Lock();
  printf("[Flash] 发射功率已保存: %d dBm\r\n", power);
}

/**
 * @brief  从 Flash 读出一个以 '\0' 结尾的字符串
 */
static void flashReadString(uint32_t addr, char *dst, int dstSize)
{
  const char *p = (const char *)addr;
  int i;
  for (i = 0; i < dstSize - 1 && p[i] != '\0'; i++)
    dst[i] = p[i];
  dst[i] = '\0';
}

/**
 * @brief  把一个字符串（含结尾 '\0'）按半字写入 Flash
 * @note   STM32F1 只能半字编程；用 i+1<=len 兜底，
 *         避免读到源串 '\0' 之后的越界字节
 */
static void flashWriteString(uint32_t addr, const char *str)
{
  int len = (int)strlen(str);
  int i;
  for (i = 0; i <= len; i += 2) {
    uint16_t halfWord = (uint16_t)str[i];
    if (i + 1 <= len)
      halfWord |= ((uint16_t)str[i + 1]) << 8;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, halfWord);
  }
}

/**
 * @brief  从 Flash 加载农场中心坐标
 * @note   如果 Flash 中有有效魔术字则加载，否则保持为空（未设置）
 */
static void loadCenterFromFlash(void)
{
  uint32_t magic = *(volatile uint32_t *)CENTER_FLASH_ADDR;
  if (magic == CENTER_MAGIC) {
    flashReadString(CENTER_FLASH_ADDR + 4, centerStr, sizeof(centerStr));
    printf("[Flash] 中心坐标已加载: %s\r\n", centerStr);
  } else {
    centerStr[0] = '\0';
    printf("[Flash] 无保存中心坐标\r\n");
  }
}

/**
 * @brief  保存农场中心坐标到 Flash（独立页，与其它配置项互不影响）
 * @param  str: 归一化后的坐标字符串，格式 "lat,lng"
 */
static void saveCenterToFlash(const char *str)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = CENTER_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CENTER_FLASH_ADDR, CENTER_MAGIC);
  flashWriteString(CENTER_FLASH_ADDR + 4, str);

  HAL_FLASH_Lock();
  printf("[Flash] 中心坐标已保存: %s\r\n", str);
}

/**
 * @brief  校验经纬度是否在合法地理范围内
 * @retval 1=合法, 0=非法
 */
static uint8_t centerCoordValid(int32_t lat, int32_t lng)
{
  if (lat < -9000000 || lat > 9000000)     /* ±90 度 ×1e5 */
    return 0;
  if (lng < -18000000 || lng > 18000000)   /* ±180 度 ×1e5 */
    return 0;
  return 1;
}

/**
  * @brief  首次初始化 LoRa 模块（从关机状态启动）
  * @note   仅在第一次 TX 前或从 deepsleep 唤醒后调用
  */
static void loraInitFirstTime(void)
{
  printf("LoRa 初始化中...\r\n");

  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  if (rf_init() != RF_OK) {
    /* 启动阶段 LoRa 偶尔会初始化不成功（SPI 时序/上电未稳）。
     * 以前是死循环闪灯等人工断电，现在改成自动复位 MCU 重跑一遍初始化：
     *   - 先给 LoRa 一个 RST 低脉冲，避免模块停在半初始化状态拖累下一次
     *   - 延时 3 秒让本次日志从串口吐完，同时等电源/晶振稳定
     *   - NVIC_SystemReset() 软件复位，等价于重新上电（不经过 NRST 引脚） */
    printf("射频初始化失败! 3 秒后复位 MCU 重跑初始化\r\n");

    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(3000);

    printf("[复位] NVIC_SystemReset() ...\r\n");
    NVIC_SystemReset();              /* noreturn，不会回来 */
  }
  printf("射频初始化成功\r\n");

  rf_set_default_para();
  rf_set_tx_power(currentTxPower);



  loraInitialized = 1;

 
  /* @note 合并成一行：MDK Lite 32KB 限制，Flash 余额紧张，长横幅打印已精简 */
  printf("[LoRa] 就绪: %d MHz SF%d BW125 CR4/5 CRC-off SW0x12 PWR=%d dBm\r\n",
         DEFAULT_FREQ / 1000000, DEFAULT_SF, currentTxPower);
}

/**
 * @brief  上报计数 +1（每发一帧 LoRa 都调一次），1 ~ TX_COUNT_MAX 回绕
 * @note   必须在组帧(snprintf)之前调用，帧里带的是递增后的值
 */
static void txCountNext(void)
{
  tx_count++;
  if (tx_count > TX_COUNT_MAX)
    tx_count = 1;
}

/**
  * @brief  发送配置消息（重启后只发一次）
  * @note   格式: MSG_TYPE_CONFIG|deviceName|config_str|计数|
  */
  static void sendConfigMessage(void)
  {
   char tx_msg[TX_LEN];
   uint32_t tx_time = 1000;

   txCountNext();
   snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
            MSG_TYPE_CONFIG, deviceName, configStr, (unsigned long)tx_count);

  rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  printf("[配置] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  发送设备信息消息（8|deviceName|设备数量|发射功率(dBm)|保留|）
 * @note   由 readinfo 指令触发：8|设备|设备数量|发射功率dm|保留
 *         最后一段目前填上报计数（与项目其它帧一致，段数固定不为空），
 *         以后要加字段（如固件版本/电量）就替换或追加在这段之后
 */
static void sendReadInfoMessage(void)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;

  txCountNext();
  /* 8|设备名|设备数T|频率HZ|功率dbm|固件版本|计数 —— 各段带单位，云端展示直读；
   * 版本号来自 DEMO_VER，升级固件时改它即可知道设备是否更新到位 */
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%uT|%luHZ|%ddbm|%s|%lu|",
           MSG_TYPE_READINFO, deviceName, (unsigned int)deviceCount,
           (unsigned long)(DEFAULT_FREQ / 1000000u),
           (int)currentTxPower, DEMO_VER, (unsigned long)tx_count);

  rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  printf("[设备信息] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  发送农场中心坐标消息（1|deviceName|lat,lng|计数|）
 * @note   本项目无 DTU，回复上报走 LoRa 回到集中器，由集中器转发云端
 *         未设置过坐标时第 3 段发 "0,0"，保证帧结构固定，
 *         云端/小程序 split('|') 不会拿到空串
 */
static void sendCenterMessage(void)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  txCountNext();
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
           MSG_TYPE_GPS, deviceName, coord, (unsigned long)tx_count);

  rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  printf("[中心坐标] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  串口字节数 → 紧凑可读格式（用于无定位时的上报帧第 4 段）
 * @param  bytes: 本轮搜星期间 GPS 串口收到的字节数
 * @param  buf:   输出缓冲（≥9 字节：最坏 "999.99M\0" = 8）
 * @retval 格式：<1024 → "NByte"；<1M → "X.XXK"；≥1M → "X.XXM"
 * @note   两位小数用整数运算（×100/1024）实现，不引入浮点库；
 *         长度上限：uint32 最大 4G → "4095.99M" 共 8 字符，buf 12 足够
 */
static void fmtRxBytes(uint32_t bytes, char *buf, uint8_t len)
{
  if (bytes < 1024u) {
    snprintf(buf, len, "%luByte", (unsigned long)bytes);
  } else if (bytes < 1024u * 1024u) {
    snprintf(buf, len, "%lu.%02luK",
             (unsigned long)(bytes / 1024u),
             (unsigned long)((bytes % 1024u) * 100u / 1024u));
  } else {
    snprintf(buf, len, "%lu.%02luM",
             (unsigned long)(bytes / (1024u * 1024u)),
             (unsigned long)((bytes % (1024u * 1024u)) * 100u / (1024u * 1024u)));
  }
}

/**
 * @brief  发送当前 GPS 实时定位坐标帧
 * @param  hasFix: 1=有有效定位（发实测坐标）, 0=搜星失败（发 "0.0,0.0"）
 * @param  useGpsType: 0=MSG_TYPE_UP_GPS(5), 1=MSG_TYPE_GPS(1)
 * @note   两种帧类型的用法（云端按帧类型 + 上报时机区分）:
 *           MSG_TYPE_UP_GPS(5) = 收到 upgps 指令后的跟踪上报，上报后不开接收窗口
 *           MSG_TYPE_GPS(1)    = GPS 工作时间段内的自动上报、以及搜星失败后的
 *                                重试上报，上报后要开 3 秒接收窗口
 *         1 同时被 C 指令的农场中心坐标回显使用，但那是另一条指令路径
 *         （收到 C 才发一次），与本函数的实时坐标不冲突
 *         搜星失败时也必须发帧，第 3 段填 "0.0,0.0"，
 *         保证云端 split('|') 拿到的段数固定，可直接判定为"无坐标"
 */
static void sendGpsPositionMessage(uint8_t hasFix, uint8_t useGpsType,
                                   uint32_t searchSec, uint32_t rxBytes)
{
  char tx_msg[TX_LEN];
  uint32_t tx_time = 1000;
  int      msgType = useGpsType ? MSG_TYPE_GPS : MSG_TYPE_UP_GPS;

  txCountNext();   /* 每发一帧 LoRa 计数 +1（类型 1/5 都算一帧） */

  if (hasFix) {
    char latBuf[16], lngBuf[16];

    /* 小数后 5 位：精度约 1.1m，够用且帧长更短 */
    deg1e5ToStr(gpsFix.lat, latBuf);
    deg1e5ToStr(gpsFix.lng, lngBuf);
    /* 有定位: 类型|设备名|lat,lng|搜星秒数|计数| */
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s,%s|%lu|%lu|",
             msgType, deviceName, latBuf, lngBuf,
             (unsigned long)searchSec, (unsigned long)tx_count);
  } else {
    char bytesBuf[12];

    /* 无定位: 类型|设备名|0.0,0.0|串口字节数|计数|
     * @note  第 4 段=本轮搜星期间 GPS 模块吐到 MCU 的字节数，
     *        诊断"模块没输出"(0 或极小)还是"有输出但没定位"(数值正常)；
     *        格式化为紧凑可读形式: 1Byte / 0.32K / 34.22M（两位小数） */
    fmtRxBytes(rxBytes, bytesBuf, sizeof(bytesBuf));
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|0.0,0.0|%s|%lu|",
             msgType, deviceName, bytesBuf, (unsigned long)tx_count);
  }

  rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
    ;
  rf_set_transmit_flag(RADIO_FLAG_IDLE);

  printf("[定位上报] 发送  -> \"%s\"\r\n", tx_msg);
}

/**
 * @brief  搜星 + 上报坐标（upgps 指令 与 失败自动重试 共用同一套流程）
 * @note   搜星期间临时打开 GPS 打印，结束后恢复成进入前的状态
 * @retval 1=拿到有效坐标并已上报, 0=搜星失败已上报 "0.0,0.0"
 */
static uint8_t gpsSearchAndReport(uint8_t useGpsType)
{
  uint8_t  hasFix;
  uint8_t  savedPrint = gpsPrintEn;   /* 用局部变量保存，连续模式下每轮都能独立开关 */
  uint32_t t0, searchSec;

  gpsPrintEn = 1;                    /* 搜星期间打开 GPS 打印 */
  gpsRxCount = 0;                    /* 本轮字节数从 0 起算（连续模式下每轮独立统计） */

  /* 搜星：最短 GPS_WAIT_MIN_MS(给中继留准备时间), 上限 GPS_WAIT_TIMEOUT_MS */
  t0 = HAL_GetTick();
  hasFix = gpsWaitForFix(GPS_WAIT_MIN_MS, GPS_WAIT_TIMEOUT_MS);
  searchSec = (HAL_GetTick() - t0 + 999u) / 1000u;   /* 搜星耗时，秒（向上取整） */
  HAL_Delay(1000);

  /* 无论是否定位成功都上报：失败时坐标段填 "0.0,0.0"，参数段填串口字节数 */
  sendGpsPositionMessage(hasFix, useGpsType, searchSec, gpsRxCount);

  /* 打印状态恢复成本轮进来看之前的样子（默认回到静默） */
  gpsPrintEn = savedPrint;
  printf("[GPS] 上报完毕%s, 恢复GPS打印=%s\r\n",
         hasFix ? "(已定位)" : "(未定位)", gpsPrintEn ? "ON" : "OFF");

  return hasFix;
}

/**
 * @brief  提前搜星（需要 GPS 的上报轮：唤醒后先搜，到点只发帧）
 * @note   1) 搜星窗口 = 现在 → (nextTxTimestamp - 5 秒)，必须留 5 秒给 LoRa 发送；
 *         2) 窗口不足 GPS_WAIT_MIN_MS(10 秒) 时直接跳过：标记"已完成、无坐标"，
 *            到点发 "0.0,0.0|串口字节数|"，绝不让上报时间点后移；
 *         3) 窗口够但小于搜星上限时以窗口为准
 *            （例如上报周期 1 分钟 < 4 分钟搜星上限，只能在 1 分钟内尽力搜）；
 *         4) 搜完立即断电，结果暂存 gpsPreXxx，OnMaster 到点直接上报，不再现场搜星
 */
static void gpsPreSearch(void)
{
  uint32_t nowTs    = getCurrentTimestamp();
  uint32_t deadline = (nextTxTimestamp > 5u) ? (nextTxTimestamp - 5u) : nowTs;
  uint32_t availMs  = (deadline > nowTs) ? ((deadline - nowTs) * 1000u) : 0u;
  uint32_t maxMs, minMs, t0;
  uint8_t  hasFix, savedPrint;

  gpsPreSearched = 1;      /* 无论搜不搜，都标记本轮预搜已处理 */
  gpsPreFixOk    = 0;
  gpsPreSec      = 0;
  gpsPreBytes    = 0;

  if (availMs < GPS_WAIT_MIN_MS) {
    printf("[GPS] 预搜窗口仅 %lu 秒(<最短 %lu 秒), 跳过搜星, 到点直接上报 0.0,0.0\r\n",
           (unsigned long)(availMs / 1000u),
           (unsigned long)(GPS_WAIT_MIN_MS / 1000u));
    return;
  }

  maxMs = (availMs > GPS_WAIT_TIMEOUT_MS) ? GPS_WAIT_TIMEOUT_MS : availMs;
  minMs = (maxMs > GPS_WAIT_MIN_MS) ? GPS_WAIT_MIN_MS : maxMs;

  printf("[GPS] 预搜窗%lus(限%lus)\r\n",
         (unsigned long)(availMs / 1000u), (unsigned long)(maxMs / 1000u));

  savedPrint = gpsPrintEn;
  gpsPrintEn = 1;          /* 搜星期间打开 GPS 打印 */
  gpsRxCount = 0;

  gpsPowerOn();
  t0 = HAL_GetTick();
  hasFix = gpsWaitForFix(minMs, maxMs);
  gpsPreSec   = (HAL_GetTick() - t0 + 999u) / 1000u;
  gpsPreBytes = gpsRxCount;
  gpsPowerOff();           /* 搜完立即断电，等上报点时再发帧 */

  gpsPrintEn = savedPrint;
  gpsPreFixOk = hasFix;
  printf("[GPS] 预搜完成: %s, 耗时 %lu 秒, 串口 %lu 字节\r\n",
         hasFix ? "(已定位, 到点直接上报缓存坐标)" : "(未定位, 到点上报 0.0,0.0)",
         (unsigned long)gpsPreSec, (unsigned long)gpsPreBytes);
}

/**
 * @brief  连续定位上报：totalMin 分钟内不休眠，每隔 GPS_CONT_INTERVAL_SEC 秒上报一次坐标
 * @note   每一轮的结构：
 *           1) gpsSearchAndReport() —— 最短 10 秒、最长 GPS_WAIT_TIMEOUT_MS 搜星后发一帧坐标
 *           2) 搜星提前结束时，把本轮补齐到 GPS_CONT_INTERVAL_SEC 秒再进下一轮
 *              （期间继续跑 gpsTask() 排空环形缓冲，但**不进入休眠**）
 *         轮数 = 1 + floor((总时长 - 已用时) / GPS_CONT_INTERVAL_SEC)：
 *           第 1 轮收到指令就立刻执行；之后每轮开始**前**先看剩余时间，
 *           不足一轮的最短占用就提前结束，回到正常定时逻辑（该休眠就休眠）
 *         —— 这样"upgps 1 分钟 + 10 秒搜到星"只上报 1 次，不会在剩下 50 秒里连报 5 次
 * @note   定位成功导向：跟踪的目标是拿到有效坐标 ——
 *           - 只要还没拿到过坐标，期限自动保底延长到 GPS_CONT_MAX_SEC(60 分钟)，
 *             期间每轮照常上报 0.0,0.0（让云端知道设备仍在跟踪中）；
 *           - 一旦拿到坐标，期限立刻收回参数设定的 totalSec：
 *             若已超时则本轮（刚报出坐标的这帧）就是最后一帧，正常收工；
 *           - 参数是 60 的失败场景天然就是 60 分钟，无需再延长
 * @warning MCU 与射频全程全速工作，分钟数直接线性影响耗电
 * @retval 1=期间至少有一轮拿到有效坐标, 0=全程都没有有效坐标
 */
static uint8_t gpsRunContinuous(uint16_t totalMin, uint8_t useGpsType)
{
  uint32_t totalSec    = (uint32_t)totalMin * 60u;
  uint32_t deadlineSec = totalSec;     /* 实际结束期限：无坐标时延长到 60 分钟 */
  uint32_t startTick   = HAL_GetTick();   /* 计时基准用开机 tick（毫秒）：
   * 连续定位期间常会发生卫星授时/服务器对时，绝对时间戳会被整体平移，
   * 用时间戳差值会把"已用时"算成天文数字，导致第 1 轮后就被误判超时结束 */
  uint32_t rounds      = 0;
  uint8_t  everFixed   = 0;

  printf("[连续定位] 开始: 时长=%u 分钟, 每轮至少 %u 秒(无坐标则跟踪保底 60 分钟)\r\n",
         (unsigned int)totalMin, (unsigned int)GPS_CONT_INTERVAL_SEC);

  while (1) {
    uint32_t slotStart = HAL_GetTick();
    uint32_t elapsed;
    uint32_t remain;
    uint8_t  hasFix;

    /* 第 1 轮无条件执行：收到 upgps 指令就是要立刻搜星上报 */
    rounds++;
    hasFix = gpsSearchAndReport(useGpsType);
    if (hasFix)
      everFixed = 1;

    if (hasFix) {
      printf("[连续定位] 第 %lu 轮已上报真实坐标\r\n", (unsigned long)rounds);

      /* 拿到坐标：期限收回参数设定值。若已超时，下面的判断会正常结束，
       * 刚报出坐标的这一帧就是最后一帧 —— 参数语义保持不变 */
      if (deadlineSec > totalSec)
        deadlineSec = totalSec;
    } else {
      printf("[连续定位] 第 %lu 轮无坐标已上报 0.0,0.0\r\n", (unsigned long)rounds);
    }

    /* 定位成功导向：至今没有有效坐标 → 跟踪期限保底延长到 60 分钟，
     * 继续按原间隔每轮上报（每帧 +1 计数），直到定位成功或到上限 */
    if (!everFixed && (deadlineSec < GPS_CONT_MAX_SEC)) {
      deadlineSec = GPS_CONT_MAX_SEC;
      printf("[连续定位] 尚无有效坐标, 跟踪期限延长到 60 分钟(每轮照常上报)\r\n");
    }

    elapsed = (HAL_GetTick() - startTick) / 1000u;   /* 毫秒 → 秒 */
    if (elapsed >= deadlineSec)
      break;                                   /* 期限已用完 */

    remain = deadlineSec - elapsed;
    if (remain < (uint32_t)GPS_CONT_INTERVAL_SEC) {
      /* 剩下的时间凑不满一轮（一轮最短也要 GPS_CONT_INTERVAL_SEC 秒），
       * 不再空等，直接结束连续定位，回到正常定时调度（该休眠就休眠） */
      printf("[连续定位] 剩余 %lu 秒 < 一轮最短 %u 秒, 结束(回到正常定时逻辑)\r\n",
             (unsigned long)remain, (unsigned int)GPS_CONT_INTERVAL_SEC);
      break;
    }

    /* 把本轮补齐到 GPS_CONT_INTERVAL_SEC 秒：搜星只用 10 秒也要占满，
     * 搜星耗时超过它则本轮自然以搜星耗时为准，不再额外等待。
     * 期间不休眠，但必须持续跑 gpsTask()，否则 512 字节环形缓冲会堆满丢句 */
    while ((HAL_GetTick() - slotStart) <
           (uint32_t)GPS_CONT_INTERVAL_SEC * 1000u) {
      gpsTask();
      HAL_Delay(100);
    }
  }

  if (everFixed)
    printf("[连续定位] 结束: 共 %lu 轮, 期间有拿到过有效坐标\r\n",
           (unsigned long)rounds);
  else
    printf("[连续定位] 结束: 共 %lu 轮, 期间始终没有有效坐标\r\n",
           (unsigned long)rounds);

  return everFixed;
}

/**
  * @brief  计算下次上报时间
  * @note   已对时：基于时隙调度，nextTx = 下一个 slot 边界（休眠时保持在 RAM）
  *         未对时：nextTx = 当前 boot seconds + 间隔（重启后重新计算）
  */
static void calcNextTxTime(void)
{
  uint32_t intervalSec = getConfigIntervalSec();
  uint8_t  fastMode = 0;

  /* 开机前 3 轮快速上报：配置周期 > 1 分钟时先压到 1 分钟，
   * 让设备尽快完成对时 / 收配置 / 进入正常时隙节奏；5 轮后恢复配置周期。
   * @note  时隙偏移也必须按压缩后的周期算，否则各设备的错峰位置会乱 */
  if ((fastStartRounds > 0) && (intervalSec > 60u)) {
    intervalSec = 60u;
    fastMode = 1;
  }

  if (timeSynced) {
    /* 已对时：基于当天 00:00 的时隙调度 */
    uint32_t slotOffset = getSlotOffsetSec(intervalSec);
    uint32_t nowSec = getTodaySecond();
    uint32_t currentSlot = nowSec / intervalSec;
    uint32_t nextSlotTime = (currentSlot + 1) * intervalSec + slotOffset;
    /* 如果计算的时间已过，再加一个周期 */
    if (nextSlotTime <= nowSec) {
      nextSlotTime += intervalSec;
    }
    /* 防止对时后等待时间过短导致重复上报（< 半周期则再加一个周期） */
    uint32_t waitSec = nextSlotTime - nowSec;
    if (waitSec < intervalSec / 2) {
      nextSlotTime += intervalSec;
    }
    nextTxTimestamp = getCurrentTimestamp() + (nextSlotTime - nowSec);
    {
      uint32_t nowHms = nowSec % 86400;
      uint32_t nextHms = nextTxTimestamp % 86400;
      printf("[SCHED] synced | now=%lu %02lu:%02lu:%02lu slot=%lu wait=%lu (%lu:%02lu) | nextTx=%lu %02lu:%02lu:%02lu\r\n",
             (unsigned long)nowSec,
             (unsigned long)(nowHms / 3600), (unsigned long)((nowHms % 3600) / 60), (unsigned long)(nowHms % 60),
             (unsigned long)slotOffset,
             (unsigned long)(nextSlotTime - nowSec),
             (unsigned long)((nextSlotTime - nowSec) / 60),
             (unsigned long)((nextSlotTime - nowSec) % 60),
             (unsigned long)nextTxTimestamp,
             (unsigned long)(nextHms / 3600), (unsigned long)((nextHms % 3600) / 60), (unsigned long)(nextHms % 60));
    }
  } else {
    /* 未对时：当前 boot seconds + 间隔 */
    nextTxTimestamp = getCurrentTimestamp() + intervalSec;
    printf("[SCHED] no-sync | interval=%lu (%lu:%02lu) | nextTx=%lu\r\n",
           (unsigned long)intervalSec,
           (unsigned long)(intervalSec / 60), (unsigned long)(intervalSec % 60),
           (unsigned long)nextTxTimestamp);
  }

  if (fastMode)
    printf("[调度] 快速上报: 3轮1分(剩%u)\r\n", (unsigned)fastStartRounds);
}

/**
 * @brief  对时成功后的调度决策（卫星授时 / SYN_TIME 两条路径共用）
 * @note   首次开机场景：本机还没发过任何 LoRa 帧（tx_count==0）时，
 *         开机的"立即上报"会被授时抢先重算掉（授时发生在首次上报之前，
 *         calcNextTxTime 把 nextTxTimestamp 推到了一个周期之后）。
 *         这里补救：立即补报一次时间帧，让云端第一时间拿到本机时间状态；
 *         上报完 OnMaster 末尾再正常消耗快速轮配额并计算休眠时间。
 *         非首次（tx_count>0，如等待期收到对时）→ 按原逻辑重算下次时间
 */
static void scheduleAfterSync(void)
{
  /* 对时使 RTC 时间基准整体跳变：链路看门狗的活性时刻必须同步刷新，
   * 否则"已用时"会被算成天文数字，对时后第一次检查就误重启 */
  lastLoRaRxTs = getCurrentTimestamp();

  if (tx_count == 0) {
    nextTxTimestamp = getCurrentTimestamp();   /* 立即 → 主循环下一圈就上报 */
    printf("[调度] 首次对时, 立即补报\r\n");
  } else {
    calcNextTxTime();
  }
}

/**
  * @brief  从 configStr 解析上报间隔（秒）
  * @note   configStr 格式: "30,0M,3t,6" → 第一个字段 30 = 30 分钟 = 1800 秒
  *         取值范围: 1~60 分钟
  */
static uint32_t getConfigIntervalSec(void)
{
  uint32_t min = (uint32_t)atoi(configStr);
  static uint8_t warned = 0;   /* 容错钳位只提示一次，避免刷屏 */

  /* 容错保护：上报周期必须在 1~60 分钟（工作时内的上报节奏不允许超过 1 小时）。
   * A 指令写入端会拒绝超范围值，这里再兜底一层：
   * Flash 里的历史数据 / 手工改写 / 字段损坏都钳到合法范围 */
  if (min == 0) {
    min = 1;                    /* 非数字或 0 → 按 1 分钟执行 */
    if (!warned) {
      warned = 1;
      printf("[配置] 上报周期非法(0 或非数字), 实际按 1 分钟执行\r\n");
    }
  } else if (min > 60) {
    if (!warned) {
      warned = 1;
      printf("[配置] 上报周期 %lu 分钟超上限(60), 实际按 60 分钟执行\r\n",
             (unsigned long)min);
    }
    min = 60;
  }
  return min * 60;
}

/* ---- 时间段编码：base62 双字符 → 索引(0~275) → 当天整点区间 ----
 * 索引枚举当天所有"起点 < 终点"的整点区间：
 *   起点 0~22, 终点 起点+1 ~ 23 (23 点即代表 24 点, 故 0~23 就是全天)
 *   总数 = 23+22+...+1 = 276, 即 idx 0~275
 * 编号方式(三角枚举): idx = 前面所有起点的区间数之和 + (终点 - 起点 - 1)
 * 实例(标准 base62 字典):
 *   "0M" → idx 22  → 00:00 ~ 24:00
 *   "1M" → idx 84  → 03:00 ~ 22:00
 *   "3t" → idx 241 → 15:00 ~ 17:00
 *   "3u" → idx 242 → 15:00 ~ 18:00
 * @warning 字典顺序必须与下发方完全一致，否则解出来的时间会偏 */
static const char TIME_DICT[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

#define TIME_HOUR_END  23   /* 终点最大取值：23 即代表 24:00 */

/**
  * @brief  取 configStr 第 idx 段的原始字符串（到下一个 ',' 为止，idx 从 0 起）
  * @note   时间段字段是 "0M" 这种双字符编码，不能用 atoi（会被截断成 0）
  */
static void getConfigFieldStrFrom(const char *src, uint8_t idx,
                                  char *out, uint8_t outLen)
{
  const char *p   = src;
  const char *end;
  uint8_t i;

  out[0] = '\0';
  for (i = 0; i < idx; i++) {
    p = strchr(p, ',');
    if (p == NULL)
      return;
    p++;                     /* 跳过逗号本身 */
  }
  end = strchr(p, ',');
  if (end == NULL)
    end = p + strlen(p);
  if ((uint8_t)(end - p) >= outLen)
    end = p + outLen - 1;
  memcpy(out, p, (size_t)(end - p));
  out[end - p] = '\0';
}

static void getConfigFieldStr(uint8_t idx, char *out, uint8_t outLen)
{
  getConfigFieldStrFrom(configStr, idx, out, outLen);
}

/**
  * @brief  双字符 → 索引（base62 解码）
  * @retval 0~275；字符不在字典中或长度不足 2 时返回 -1
  */
static int16_t twoCharToIndex(const char *s)
{
  const char *p0, *p1;

  if (s == NULL || s[0] == '\0' || s[1] == '\0')
    return -1;
  p0 = strchr(TIME_DICT, s[0]);
  p1 = strchr(TIME_DICT, s[1]);
  if (p0 == NULL || p1 == NULL)
    return -1;
  return (int16_t)((p0 - TIME_DICT) * 62 + (p1 - TIME_DICT));
}

/**
 * @brief  校验配置里的时间段字段（工作时段 / GPS 时段）是否合法
 * @param  f: 单个字段字符串（由 getConfigFieldStr 按逗号切出）
 * @retval 1=合法, 0=非法
 * @note   合法 = 恰好 2 个字符，且都在 base62 字典内。
 *         LoRa 空中乱码可能把 "0M" 变成 "0"、"0Mx"、"0?" 之类，
 *         必须在写入 Flash 前拦下，否则时段判断永远失效还难以排查
 */
static uint8_t configTimeFieldValid(const char *f)
{
  if (f == NULL)
    return 0;
  if (f[0] == '\0' || f[1] == '\0' || f[2] != '\0')
    return 0;                        /* 空 / 1 位 / 3 位以上 都非法 */
  if (strchr(TIME_DICT, f[0]) == NULL || strchr(TIME_DICT, f[1]) == NULL)
    return 0;                        /* 字符不在 base62 字典内 */
  return 1;
}

/**
  * @brief  索引 → 当天整点区间（三角枚举的解码）
  * @param[out] startHour 起点小时 0~22
  * @param[out] endHour   终点小时 起点+1~23（23 表示 24:00）
  * @retval 1=成功, 0=索引非法
  */
static uint8_t timeIdxToRange(int16_t idx, uint8_t *startHour, uint8_t *endHour)
{
  int16_t base = 0;
  uint8_t s;

  if (idx < 0 || idx > 275)
    return 0;

  for (s = 0; s < TIME_HOUR_END; s++) {
    int16_t cnt = (int16_t)(TIME_HOUR_END - s);   /* 该起点下的区间个数 */

    if (idx < base + cnt) {
      *startHour = s;
      *endHour   = (uint8_t)(s + 1 + (idx - base));
      return 1;
    }
    base += cnt;
  }
  return 0;
}

/**
  * @brief  工作时间段索引（configStr 第 2 段："0M" → 22）
  */
static int16_t getConfigWorkStartIdx(void)
{
  char f[8];
  getConfigFieldStr(1, f, sizeof(f));
  return twoCharToIndex(f);
}

/**
  * @brief  GPS 自动上报时间段索引（configStr 第 3 段："3t" → 241）
  */
static int16_t getConfigGpsEndIdx(void)
{
  char f[8];
  getConfigFieldStr(2, f, sizeof(f));
  return twoCharToIndex(f);
}

/**
  * @brief  当前是否处于 GPS 工作时间段（configStr 第 3 段，如 "3t" → 15:00~17:00）
  * @retval 1=在时间段内, 0=不在 / 未对时 / 字段非法
  * @note   未对时直接返回 0：此时 getTodaySecond() 是开机秒数，
  *         拿它判小时会落到错误时段，宁可不上报也不误唤醒 GPS
  */
static uint8_t isInGpsWorkWindow(void)
{
  return isInGpsWorkWindowAt(getCurrentTimestamp());
}

/**
 * @brief  指定时刻是否处于 GPS 工作时间段（configStr 第 3 段）
 * @param  ts: Unix 时间戳（秒）；用于提前判断"下次上报时刻"是否要搜星
 * @retval 1=在时间段内, 0=不在 / 未对时 / 字段非法
 * @note   未对时时返回 0（时间基准是开机秒数，判断无意义）
 */
static uint8_t isInGpsWorkWindowAt(uint32_t ts)
{
  uint8_t  s, e;
  uint32_t hour;

  if (!timeSynced)
    return 0;
  if (!timeIdxToRange(getConfigGpsEndIdx(), &s, &e))
    return 0;
  if (e == TIME_HOUR_END)
    e = 24;                          /* 23 表示 24:00 */

  hour = (ts % 86400u) / 3600u;
  return ((hour >= s) && (hour < e)) ? 1U : 0U;
}

/**
 * @brief  指定上报时刻是否是"必须准点"的 GPS 轮（类型 1）
 * @param  ts: 下次上报的 Unix 时间戳
 * @retval 1=准点 GPS 轮（上轮失败重试 / 该时刻在 GPS 时间段内）
 * @note   两类 GPS 轮的定位不同：
 *         1) 类型 1（本函数返回 1）：定时上报，**必须准点** —— 提前搜星，
 *            到点无论有无坐标都发帧，时间点绝不允许后移；
 *         2) 类型 5（gpsWaitReport，upgps 指令触发）：**不受时间约束** ——
 *            醒后现场搜星，搜多久算多久，上报时间可以后移到搜星结束，
 *            所以不参与提前量计算（返回 0）
 */
static uint8_t gpsNeededTimelyAt(uint32_t ts)
{
  if (gpsWaitReport)
    return 0;                        /* 类型5：upgps 不受约束，现场搜 */
  if (gpsNeedRetry)
    return 1;                        /* 类型1：上轮失败，本轮准点重试 */
  return isInGpsWorkWindowAt(ts);
}

/**
  * @brief  当前是否处于工作时间段（configStr 第 2 段，如 "0M" → 00:00~24:00）
  * @retval 1=在工作时间内, 0=不在 / 未对时 / 字段非法
  * @note   GPS 工作时间(第 3 段)一定落在工作时间(第 2 段)之内，
  *         所以 isInGpsWorkWindow()=1 时本函数必然也是 1
  */
static uint8_t isInWorkWindow(void)
{
  uint8_t  s, e;
  uint32_t hour;

  if (!timeSynced)
    return 0;
  if (!timeIdxToRange(getConfigWorkStartIdx(), &s, &e))
    return 0;
  if (e == TIME_HOUR_END)
    e = 24;                          /* 23 表示 24:00 */

  hour = getTodaySecond() / 3600u;
  return ((hour >= s) && (hour < e)) ? 1U : 0U;
}

/**
 * @brief  链路沉默看门狗检查：工作时间内 2 小时没收到任何 LoRa 信息 → 真复位
 * @note   - 计时基准是 RTC 绝对时间戳，跨 STOP 深度休眠连续 —— 深度休眠机制
 *           完全不变，睡 30 分钟醒来这轮检查照样能覆盖休眠期（醒来必过这里）
 *         - lastLoRaRxTs==0 表示开机后还没初始化：取当前时刻起算
 *         - 对时成功会使时间基准跳变，scheduleAfterSync() 里同步刷新，防误判
 *         - 连续定位(upgps 跟踪)期间跳过：跟踪是主动任务，类型 5 本来就不
 *           开接收窗口，60 分钟 < 2 小时也不会触发，跳过保证绝不打断跟踪
 *         - 触发动作是 NVIC_SystemReset() 真复位（不是 STOP 唤醒）：
 *           快速上报 3 轮、配置上报、对时全部重走一遍，相当于设备自愈
 */
static void linkSilenceCheck(void)
{
  uint32_t now;
  uint32_t silent;

  if (gpsContTotalMin > 0)
    return;                              /* 跟踪中不检查 */

  if (!isInWorkWindow())
    return;                              /* 只在工作时间内生效 */

  now = getCurrentTimestamp();
  if (lastLoRaRxTs == 0) {
    lastLoRaRxTs = now;                  /* 开机首次检查：从当前时刻起算 */
    return;
  }

  silent = now - lastLoRaRxTs;           /* 无符号差值，回绕安全 */
  if (silent >= LINK_SILENCE_REBOOT_SEC) {
    printf("[看门狗] %lu分%lu秒无LoRa, 重启自恢复\r\n",
           (unsigned long)(silent / 60u), (unsigned long)(silent % 60u));
    HAL_Delay(100);                      /* 让日志从串口发完 */
    NVIC_SystemReset();
  }
}

/**
  * @brief  打印当前生效的两个时间段（工作时间 / GPS 上报时间）
  * @param  tag: 行首前缀，用于区分场景（启动时 / 收到新配置时）
  * @note   时段判断每次都是从 configStr 实时解析的，所以收到 A 指令更新配置后
  *         **立即生效**，不存在"要重新加载"的问题，这里只是把结果打出来核对
  */
static void printTimeWindows(const char *tag)
{
  char    f0[8], f1[8];
  int16_t i0, i1;
  uint8_t s0, e0, s1, e1;

  getConfigFieldStr(1, f0, sizeof(f0));   /* 第 2 段: 工作时间段 */
  getConfigFieldStr(2, f1, sizeof(f1));   /* 第 3 段: GPS 上报时间段 */
  i0 = getConfigWorkStartIdx();
  i1 = getConfigGpsEndIdx();

  if (timeIdxToRange(i0, &s0, &e0) && timeIdxToRange(i1, &s1, &e1)) {
    /* 终点 23 表示 24:00 */
    printf("%s工作 %02u:00 ~ %02u:00 (%s->idx %d), GPS上报 %02u:00 ~ %02u:00 (%s->idx %d)\r\n",
           tag,
           s0, (e0 == TIME_HOUR_END) ? 24u : e0, f0, i0,
           s1, (e1 == TIME_HOUR_END) ? 24u : e1, f1, i1);
  } else {
    printf("%s解析失败, 时段判断不生效 (段2=%s->idx %d, 段3=%s->idx %d)\r\n",
           tag, f0, i0, f1, i1);
  }
}

/**
  * @brief  从 deviceName 解析设备序号
  * @note   "v0-0" → 0, "v0-1" → 1, "v0-27" → 27
  *         格式: "vX-Y" 取 '-' 后面的数字
  */
static uint8_t getDeviceIndex(void)
{
  char *dash = strchr(deviceName, '-');
  if (dash != NULL) {
    return (uint8_t)atoi(dash + 1);
  }
  return 0;
}

/**
  * @brief  计算本设备在上报周期中的时隙偏移（秒）
  * @note   公式: deviceIndex / deviceCount * intervalSec
  *         总台数 deviceCount 默认 30，可由 devicecount 指令下发（1~60，持久化）
  *         如: v0-27, 30 台, 30 分钟周期 → 27/30*1800 = 1740 秒
  * @note   序号超出总台数时按 deviceCount 取模绕回，保证偏移仍落在本周期内，
  *         否则 offset 会超过 intervalSec，时隙滑到下一个周期去
  */
static uint32_t getSlotOffsetSec(uint32_t intervalSec)
{
  uint8_t  index = getDeviceIndex();

  if (deviceCount == 0)          /* 防御：不会发生（加载/指令都已保证 >=1） */
    return 0;

  index = (uint8_t)(index % deviceCount);
  return (uint32_t)index * intervalSec / deviceCount;
}

/* ================= RTC：STOP 深度休眠的时间基准 =================
 * F1 的 RTC 是 32 位秒计数器，STOP 模式下继续走，替代 HAL_GetTick 做系统时钟。
 * 时钟源优先 LSE(32.768k, ±20ppm)；板上无晶振则退 LSI(40k, 误差~1%)，
 * LSI 的偏差靠每轮上报后的 nettime 对时重新校准，不跨轮累计。
 * STOP 唤醒源 = RTC 闹钟中断(EXTI line 17)；唤醒后系统时钟回落 HSI 8MHz，
 * 必须重新 SystemClock_Config() 才能恢复 72MHz 和外设波特率 */
static uint32_t rtcPrl = 32767;  /* 预分频-1：LSE=32767，LSI=39999 */

/* 等 RTC 写操作完成（RTOFF=1） */
static void rtc_wait_rtoff(void)
{
  uint32_t guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RTOFF) && --guard)
    ;
}

/* 等 APB1 与 RTC 接口重新同步（RSF 置位）
 * @note  系统复位或 STOP 唤醒后必须先等 RSF，否则读 RTC 计数得到 0！ */
static void rtc_wait_rsf(void)
{
  uint32_t guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard)
    ;
}

/* 读 32 位秒计数（先读高 16 位，硬件锁存低 16 位）
 * @note  STOP 唤醒后 RTC 接口需要 2 个 RTC 时钟重新同步，
 *        必须软件清 RSF 再等硬件重新置位，否则读到休眠前的影子值！ */
static uint32_t rtc_read_counter(void)
{
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;   /* 清同步标志，强制重新同步 */
  (void)RTC->CRL;
  rtc_wait_rsf();
  return ((uint32_t)RTC->CNTH << 16) | (uint32_t)RTC->CNTL;
}

/* 清 RTC 标志（ALRF/SECF）
 * @note  RM0008 要求：写 0 清标志后必须紧跟一次读操作，
 *        且写前必须等 RTOFF=1（实测缺失尾读会导致 ALRF 后续无法置位） */
static void rtc_clear_alarm_flag(void)
{
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~(RTC_CRL_ALRF | RTC_CRL_SECF);
  (void)RTC->CRL;
}

/* 设置闹钟（绝对 RTC 秒）
 * @note  20+ 轮硬件实测结论（本芯片批次）：
 *        1) ALRIE=1 会抑制 ALRF 置位与 EXTI17 事件，ALRIE 必须保持 0！
 *           （ALRIE=0 时 ALRF 置位本身就能产生 EXTI17 事件）
 *        2) 清标志必须"等 RTOFF → 写 0 → 尾读"，见 rtc_clear_alarm_flag
 *        3) STOP 唤醒不用 WFI+NVIC（实测不醒），用 WFE+EXTI17 事件，见
 *           enterStopUntil */
static void rtc_set_alarm(uint32_t alarmSec)
{
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;              /* 进入配置模式 */
  RTC->ALRH = (uint16_t)(alarmSec >> 16);
  RTC->ALRL = (uint16_t)(alarmSec & 0xFFFFU);
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;   /* 退出配置模式 */
  (void)RTC->CRL;                       /* 尾读 */
  rtc_wait_rtoff();
  RTC->CRH &= (uint16_t)~RTC_CRH_ALRIE; /* 确保 ALRIE=0（见函数头注释） */
}

/* 确保所选 RTC 时钟源正在运行：
 * RTCSEL 在备份域（掉电保持），但 LSI 使能位在主电源域、每次上电=0！
 * 若备份域已选 LSI 而本不上电没重开 LSI，RTC 将无时钟、计数恒 0 */
static void rtc_ensure_clock_running(void)
{
  uint32_t guard;
  uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;

  if (sel == RCC_BDCR_RTCSEL_LSI) {
    RCC->CSR |= RCC_CSR_LSION;
    guard = 200000;
    while (!(RCC->CSR & RCC_CSR_LSIRDY) && --guard)
      ;
  } else if (sel == RCC_BDCR_RTCSEL_LSE) {
    RCC->BDCR |= RCC_BDCR_LSEON;        /* 备份域本应保留，保险重开 */
    guard = 8000000;
    while (!(RCC->BDCR & RCC_BDCR_LSERDY) && --guard)
      ;
  }
}

/* RTC 初始化：备份域 + 时钟源 + 预分频 + 闹钟中断
 * 备份域已配置过（软件复位/唤醒）则保留计数，不破坏时间 */
static void rtc_init(void)
{
  uint32_t guard;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  SET_BIT(PWR->CR, PWR_CR_DBP);         /* 解锁备份域写保护 */

  if ((RCC->BDCR & RCC_BDCR_RTCSEL) == 0) {
    /* 备份域未配置：选时钟源并初始化
     * @note  等待窗口必须足够长（32.768k 晶振起振可到数秒），
     *        太短会把正常晶振误判失败而退到 LSI，且 RTCSEL 写进备份域后
     *        不复位备份域就改不回来 */
    RCC->BDCR |= RCC_BDCR_LSEON;
    guard = 120000000;                   /* 72MHz 下约 4~8 秒 */
    while (!(RCC->BDCR & RCC_BDCR_LSERDY) && --guard)
      ;

    if (guard) {                        /* LSE 起振成功 */
      rtcPrl = 32767;
      RCC->BDCR |= RCC_BDCR_RTCSEL_LSE;
    } else {                            /* 无 LSE 晶振 → LSI */
      RCC->BDCR &= ~RCC_BDCR_LSEON;
      RCC->CSR |= RCC_CSR_LSION;
      guard = 200000;
      while (!(RCC->CSR & RCC_CSR_LSIRDY) && --guard)
        ;
      rtcPrl = 39999;
      RCC->BDCR |= RCC_BDCR_RTCSEL_LSI;
    }
    RCC->BDCR |= RCC_BDCR_RTCEN;

    rtc_wait_rtoff();
    RTC->CRL |= RTC_CRL_CNF;
    RTC->PRLH = (uint16_t)(rtcPrl >> 16);
    RTC->PRLL = (uint16_t)(rtcPrl & 0xFFFFU);
    RTC->CNTH = 0;
    RTC->CNTL = 0;
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
    rtc_wait_rtoff();
  }

  /* 无条件确保所选时钟源在跑（主电源域的 LSI 每次上电都要重开） */
  rtc_ensure_clock_running();

  /* APB1 复位后等 RTC 与 APB 同步完成 */
  guard = 200000;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard)
    ;

  /* RTC 闹钟 → EXTI line 17 事件：STOP 的唤醒源
   * @note  用 WFE 事件模式（EMR）而非中断模式（IMR+NVIC）：
   *        实测本芯片 STOP 下 WFI+NVIC 唤醒不工作，WFE 事件唤醒正常；
   *        ALRIE 保持 0（=1 会抑制 ALRF/事件，见 rtc_set_alarm 注释） */
  EXTI->PR  = EXTI_PR_PR17;             /* 清挂起 */
  EXTI->RTSR |= EXTI_RTSR_TR17;         /* 上升沿 */
  EXTI->EMR |= EXTI_EMR_MR17;           /* 事件模式：直接唤醒 WFE */
  EXTI->IMR &= ~EXTI_IMR_MR17;          /* 关中断模式（不走 NVIC） */
  RTC->CRH &= (uint16_t)~RTC_CRH_ALRIE; /* 确保 ALRIE=0 */
}

/* 进入 STOP 深度休眠，直到时间到达 wakeTs
 * @param wakeTs: Unix 时间戳；未对时时是 RTC 计数秒（两者此时同域）
 * @note  已对时后必须先把 Unix 时间戳换算回 RTC 计数域
 *        （RTC 计数 ≠ Unix 时间戳，直接比较会把闹钟设到几十年后！）；
 *        唤醒机制 = WFE + EXTI17 事件（实测本芯片 WFI+NVIC 唤醒不醒）；
 *        唤醒后时钟回落 HSI 8MHz、SysTick 停走，
 *        由调用方判断到点后 SystemClock_Config() 恢复 */
static void enterStopUntil(uint32_t wakeTs)
{
  uint32_t rtcNow  = rtc_read_counter();
  uint32_t wakeRtc = wakeTs;

  if (timeSynced) {
    uint32_t realDelta = wakeTs - syncedTimestamp;
    uint32_t localDelta = realDelta;

    /* 晶振偏差补偿（收敛后生效）：目标是真实时刻 wakeTs，
     * 本地 RTC 慢 d 秒/小时 → 本地计数只需走
     * realDelta*(3600+d)/3600 就能到达对应的真实时刻。
     * 例: d=-216, 真实睡 1800 秒 → 本地闹钟只设 1692 秒, 醒来正好准点 */
    if (driftConverged && rtcDriftPerHour != 0 &&
        realDelta > 0 && realDelta < 1000000)
      localDelta = realDelta * (3600u + (uint32_t)rtcDriftPerHour) / 3600u;

    wakeRtc = syncedRtcSec + localDelta;
  }

  /* 到点保护：唤醒时刻已过就直接返回，绝不能把闹钟设在过去
   * （F1 闹钟设在过去要等计数回绕 136 年才触发 = 永久沉睡） */
  if (wakeRtc <= rtcNow + 1)
    return;

  {
    uint32_t delta = wakeRtc - rtcNow;

    rtc_set_alarm(rtcNow + delta);
  }

  /* WFE 事件唤醒循环：防假唤醒（event 寄存器残留/意外事件），
   * 每次醒来核对时间，未到点继续睡 */
  for (;;) {
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFE);
    rtc_clear_alarm_flag();               /* 等 RTOFF → 写 0 → 尾读 */
    if (rtc_read_counter() >= wakeRtc)
      break;
  }
  /* 从这里继续 = 时间到点 */
}

/**
  * @brief  获取当前"当天秒数"
  * @note   未同步时：把开机当作 00:00，返回开机秒数
  *         已同步后：返回真实时间戳 % 86400
  */
static uint32_t getTodaySecond(void)
{
  return getCurrentTimestamp() % 86400;
}

/**
  * @brief  获取当前完整时间戳（基准 = RTC 秒计数器）
  * @note   未同步时返回 RTC 计数本身（开机秒数）；
  *         已同步后 = 对时时间戳 + RTC 走过的秒数（STOP 期间也连续）；
  *         偏差收敛后（driftConverged）再做晶振补偿：本地走了 L 秒、
  *         偏差 d 秒/小时时真实过了 L*3600/(3600+d) 秒 —— 本机时间
  *         回到真实轴，帧内时间戳/时段判定/网格计算全部自动正确。
  *         int64 中间量：RTC 累计可达数百万秒，×3600 会超 uint32
  */
static uint32_t getCurrentTimestamp(void)
{
  uint32_t rtcNow = rtc_read_counter();

  if (!timeSynced)
    return rtcNow;
  if (driftConverged && rtcDriftPerHour != 0) {
    /* int32 运算即可：RTC 累计秒受链路看门狗(2小时重启)约束，
     * ×3600 ≈ 3×10^7 远小于 2^31；M3 硬件除法，体积几乎为零 */
    int32_t localDelta = (int32_t)(rtcNow - syncedRtcSec);
    if (localDelta > 0 && localDelta < 1000000) {   /* ~11 天内有效 */
      int32_t realDelta = localDelta * 3600 / (3600 + rtcDriftPerHour);
      return syncedTimestamp + (uint32_t)realDelta;
    }
  }
  return syncedTimestamp + (rtcNow - syncedRtcSec);
}

/**
  * @brief  判断闰年
  */
static uint8_t isLeapYear(uint16_t year)
{
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/**
  * @brief  将 Unix 时间戳格式化为 "YYYY-MM-DD HH:MM:SS"
  */
static void formatTimestamp(uint32_t ts, char *buf, uint8_t len)
{
  uint32_t sec = ts % 60; ts /= 60;
  uint32_t min = ts % 60; ts /= 60;
  uint32_t hour = ts % 24; ts /= 24;

  uint16_t year = 1970;
  uint8_t month = 1;
  uint8_t day = 1;

  while (1) {
    uint16_t days_in_year = isLeapYear(year) ? 366 : 365;
    if (ts < days_in_year) break;
    ts -= days_in_year;
    year++;
  }

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

/**
 * @brief  Unix 时间戳 → 年份（用于对时有效性校验）
 * @note   ts=0 → 1970；uint32 最大值约对应 2106 年，循环不会死
 */
static uint16_t timestampYear(uint32_t ts)
{
  uint32_t days = ts / 86400u;
  uint16_t year = 1970;

  while (days >= (isLeapYear(year) ? 366u : 365u)) {
    days -= isLeapYear(year) ? 366u : 365u;
    year++;
  }
  return year;
}

/**
 * @brief  NMEA 坐标 ddmm.mmmm / dddmm.mmmm → 十进制度 ×1e5
 * @param  v:   原始字段，如 "2629.5800"
 * @param  dir: 'N'/'S'/'E'/'W'，南纬和西经取负
 * @retval 度 ×1e5（如 26.493 → 264930）；格式异常返回 0
 * @note   纯整数实现，避免拉入浮点解析库：度×1e5 = deg×1e5 + 分×1e4/6
 */
static int32_t nmeaToDeg(const char *v, char dir)
{
  const char *dot = strchr(v, '.');
  int32_t    deg = 0;
  uint32_t   minE4 = 0;   /* 分 ×1e4，如 29.58 分 → 295800 */
  uint32_t   mul = 100000;
  int32_t    d;

  if (dot == NULL || dot - v < 3)
    return 0;

  /* 整数部分去掉末 2 位即度 */
  while (v < dot - 2) {
    if (*v < '0' || *v > '9')
      return 0;
    deg = deg * 10 + (*v++ - '0');
  }
  v = dot - 2;          /* 分钟整数部分 2 位 */
  for (d = 0; d < 2; d++) {
    if (*v < '0' || *v > '9')
      return 0;
    minE4 += (uint32_t)(*v++ - '0') * mul;
    mul /= 10;
  }
  if (*v == '.')
    v++;
  while (mul && *v >= '0' && *v <= '9') {  /* 分钟小数部分 */
    minE4 += (uint32_t)(*v++ - '0') * mul;
    mul /= 10;
  }

  d = (int32_t)deg * 100000 + (int32_t)((minE4 + 3) / 6);  /* +3 四舍五入 */
  if (dir == 'S' || dir == 'W')
    d = -d;
  return d;
}

/**
 * @brief  度 ×1e5 → "[-]dd.ddddd" 字符串（等价于 %.5f 输出，补足 5 位小数）
 */
static void deg1e5ToStr(int32_t v, char *buf)
{
  uint32_t a = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;

  if (v < 0)
    *buf++ = '-';
  snprintf(buf, 14, "%lu.%05lu",
           (unsigned long)(a / 100000UL), (unsigned long)(a % 100000UL));
}

/**
 * @brief  解析 "26.52958" / " -26.52958" → 度 ×1e5（跳过前导空格，带四舍五入）
 * @retval 1=成功, 0=格式非法
 */
static uint8_t parseDeg1e5(const char *s, int32_t *out)
{
  int32_t  sign = 1, deg = 0;
  uint32_t frac = 0, fracMul = 100000;
  uint8_t  seenDigit = 0;

  while (*s == ' ')
    s++;
  if (*s == '-') { sign = -1; s++; }
  else if (*s == '+') s++;

  while (*s >= '0' && *s <= '9') {
    deg = deg * 10 + (*s++ - '0');
    seenDigit = 1;
  }
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9') {
      if (fracMul) {
        frac += (uint32_t)(*s - '0') * fracMul;
        fracMul /= 10;
        if (fracMul == 0 && s[1] >= '5' && s[1] <= '9')
          frac++;   /* 第 6 位小数四舍五入 */
      }
      s++;
    }
  }
  if (!seenDigit)
    return 0;
  while (*s == ' ')
    s++;
  if (*s != '\0' && *s != ',')
    return 0;

  *out = sign * ((int32_t)deg * 100000 + (int32_t)frac);
  return 1;
}

/**
 * @brief  取 NMEA 语句里第 n 个逗号分隔字段（n 从 1 开始，即第 1 个逗号之后）
 * @note   字段尾遇到 ',' 或 '*'（校验和起始）即结束
 * @retval 1=取到非空字段, 0=字段不存在或为空（dst 置空串）
 */
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen)
{
  const char *p = line;
  uint8_t i = 0;

  dst[0] = '\0';
  while (n > 0) {
    p = strchr(p, ',');
    if (p == NULL)
      return 0;
    p++;
    n--;
  }
  while (*p != '\0' && *p != ',' && *p != '*' && i < dstLen - 1)
    dst[i++] = *p++;
  dst[i] = '\0';

  return (i > 0);
}

/**
 * @brief  解析一条 NMEA 语句并打印
 * @note   支持 GGA（定位质量/经纬度/海拔/卫星数）与 RMC（日期/速度）
 *         $GPxxx / $GNxxx / $GBxxx 三种前缀都认，兼容 GPS/北斗/多模合一模块
 */
static void parseNmeaLine(char *line)
{
  /* @note 不再打印 NMEA 原始语句：搜星期间的观察改由 gpsPrintSearchStatus()
   *       每秒输出一行状态（坐标/年份/可靠性/卫星数/已搜时长/时间） */

  /* ---- GGA: 时间、定位质量、经纬度、海拔、卫星数 ---- */
  if (!strncmp(line, "$GPGGA", 6) || !strncmp(line, "$GNGGA", 6) ||
      !strncmp(line, "$GBGGA", 6)) {
    char utc[12], lat[16], latDir[4], lng[16], lngDir[4];
    char qual[4], sats[4];

    nmeaField(line, 1, utc, sizeof(utc));
    nmeaField(line, 2, lat, sizeof(lat));
    nmeaField(line, 3, latDir, sizeof(latDir));
    nmeaField(line, 4, lng, sizeof(lng));
    nmeaField(line, 5, lngDir, sizeof(lngDir));
    nmeaField(line, 6, qual, sizeof(qual));
    nmeaField(line, 7, sats, sizeof(sats));

    /* qual: 0=无效, 1=单点定位, 2=差分, 4=RTK固定, 5=RTK浮点 */
    if (atoi(qual) > 0 && lat[0] != '\0' && lng[0] != '\0') {
      gpsFix.valid = 1;
      gpsFix.lat   = nmeaToDeg(lat, latDir[0]);
      gpsFix.lng   = nmeaToDeg(lng, lngDir[0]);
      gpsFix.sats  = (uint8_t)atoi(sats);
      gpsFixValid  = 1;
      gpsFixTick   = HAL_GetTick();   /* 刷新定位时间戳，供新鲜度判断 */
      (void)utc;                      /* 时间统一取 RMC 的，这里不用 */
    } else {
      gpsFix.valid = 0;
      gpsFixValid  = 0;
      //printf("[GPS] 未定位 (quality=%s sat=%s)\r\n", qual, sats);
    }
  }
  /* ---- RMC: 日期、地面速度（GGA 里没有） ---- */
  else if (!strncmp(line, "$GPRMC", 6) || !strncmp(line, "$GNRMC", 6)) {
    char status[4], spd[12], date[8], utc[16];

    nmeaField(line, 1, utc, sizeof(utc));     /* HHMMSS.sss */
    nmeaField(line, 2, status, sizeof(status));
    nmeaField(line, 7, spd, sizeof(spd));
    nmeaField(line, 9, date, sizeof(date));   /* DDMMYY */

    /* 时间/日期无论定位与否都更新：未定位时 RMC 仍带 UTC 时间，
     * "年份是否可信"这个判断就靠它 */
    if (strlen(utc) >= 6) {
      gpsFix.hour   = (uint8_t)((utc[0] - '0') * 10 + (utc[1] - '0'));
      gpsFix.minute = (uint8_t)((utc[2] - '0') * 10 + (utc[3] - '0'));
      gpsFix.second = (uint8_t)((utc[4] - '0') * 10 + (utc[5] - '0'));

      /* 小数部分 → 毫秒（最多取 3 位） */
      gpsFix.msec = 0;
      if (utc[6] == '.') {
        uint8_t i;
        uint16_t mul = 100;

        for (i = 7; (i < strlen(utc)) && (mul > 0); i++) {
          if ((utc[i] >= '0') && (utc[i] <= '9')) {
            gpsFix.msec += (uint16_t)((utc[i] - '0') * mul);
            mul = (uint16_t)(mul / 10);
          }
        }
      }
    }
    if (strlen(date) == 6) {
      gpsFix.day   = (uint8_t)((date[0] - '0') * 10 + (date[1] - '0'));
      gpsFix.month = (uint8_t)((date[2] - '0') * 10 + (date[3] - '0'));
      gpsFix.year  = (uint16_t)(2000 + (date[4] - '0') * 10 + (date[5] - '0'));
    }
    (void)status;   /* 速度/海拔不再解析（未使用），避免拉入 atof 浮点库 */
    (void)spd;
  }
}

/**
 * @brief  当前是否持有"新鲜"的有效定位（供周期上报判断）
 * @note   光有 gpsFixValid 不够：若模块掉电或天线被遮挡导致不再输出 NMEA，
 *         gpsFixValid 会一直停在 1，从而反复上报过期坐标。
 *         故要求最近 GPS_FIX_TTL_MS 内刷新过一次。
 * @retval 1=可上报定位, 0=应回退到原时间帧
 */
static uint8_t gpsFixFresh(void)
{
  if (!gpsFixValid)
    return 0;
  if (HAL_GetTick() - gpsFixTick > GPS_FIX_TTL_MS)
    return 0;
  return 1;
}

/**
 * @brief  GPS 任务：把环形缓冲里的字节拼成完整语句后解析
 * @note   在主循环里调用；ISR 只负责往环形缓冲塞字节。
 *         语句以 '\n' 结尾，遇到 '\r' 直接丢弃（NMEA 的 CRLF）。
 *         若语句超过 GPS_LINE_LEN 视为异常，丢弃并等待下一个 '\n' 重新同步。
 */
static void gpsTask(void)
{
  /* 只在搜星(上电)期间处理 GPS 串口数据：
   * 断电状态下模块本应静默；当前硬件存在人为上拉常供电，
   * 这里统一丢弃字节，保证授时/定位解析只发生在搜星窗口内
   * （之后 PB15 电源通路接好，断电即真静默，此逻辑天然兼容） */
  if (!gpsPowerState) {
    gpsRd = gpsWr;        /* 丢弃断电期间收到的所有字节 */
    gpsLineLen = 0;
    return;
  }

  while (gpsRd != gpsWr) {
    uint8_t b = gpsRing[gpsRd];
    gpsRd = (gpsRd + 1u) % GPS_RING_LEN;

    if (b == '\n') {
      if (gpsLineLen > 0) {
        gpsLine[gpsLineLen] = '\0';
        parseNmeaLine(gpsLine);
        gpsLineLen = 0;
      }
    } else if (b != '\r') {
      if (gpsLineLen < GPS_LINE_LEN - 1)
        gpsLine[gpsLineLen++] = (char)b;
      else
        gpsLineLen = 0;  /* 超长异常，丢弃重来 */
    }
  }

  /* 未定位时的心跳提示已取消：搜星期间的观察统一由 gpsPrintSearchStatus()
   * 每秒一行状态取代，不再刷"等待定位...已收 xxx 字节" */

  /* 卫星授时兜底：本机还没对时且 GPS 给出可信日期 → 用一次卫星时间
   * （能走到这里必然处于搜星窗口，授时天然只发生在搜星过程中） */
  gpsTrySyncFromSat();
}

/**
 * @brief  GPS 日期时间(UTC) → Unix 时间戳（秒）
 * @note   仅用于 GPS 授时，1970-01-01 起算；年范围限制在 2026~2099 内无需优化
 */
static uint32_t gpsUtcToTimestamp(uint16_t year, uint8_t mon, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second)
{
  uint32_t days = 0;
  uint16_t y;
  uint8_t  m;

  for (y = 1970; y < year; y++)
    days += isLeapYear(y) ? 366u : 365u;

  for (m = 1; m < mon; m++) {
    days += gpsDaysInMonth[m - 1];
    if ((m == 2) && isLeapYear(year))
      days += 1;                    /* 闰年 2 月 +1 天 */
  }
  days += (uint32_t)(day - 1);

  return days * 86400u + (uint32_t)hour * 3600u
         + (uint32_t)minute * 60u + (uint32_t)second;
}

/**
 * @brief  GPS 卫星授时兜底（只在"本机从未对时"时用一次）
 * @note   适用场景：设备上电后一直收不到集中器/云端下发对时（SYN_TIME），
 *         但这次搜到了星 —— 卫星时间就是可信源，直接拿来对一次时。
 *         条件（全部满足才授时，任何一条不满足都直接返回）：
 *           1) timeSynced == 0  —— 本机还没对过时；已对时后永不覆盖
 *              （服务器/微信端授时优先，卫星授时只是兜底）
 *           2) gpsFixValid == 1 —— 本次已成功定位（避免拿无定位的乱日期）
 *           3) year > 2025      —— 日期可信（模块冷启动默认日期是 2000 左右）
 *         时区：RMC 输出 UTC，+8 小时得北京时间，与 SYN_TIME 时间戳同基准
 */
static void gpsTrySyncFromSat(void)
{
  uint32_t ts, nowTick;

  if (timeSynced)
    return;                         /* 已对时：不接收卫星授时 */
  if (!gpsFixValid)
    return;                         /* 没定位：日期不可信 */
  if (gpsFix.year <= 2025)
    return;                         /* 年份不对：模块日期未同步 */
  if (gpsFix.year > 2032)
    return;                         /* 年份越界：与对时校验规则一致，视为无效 */
  if (gpsFix.month < 1 || gpsFix.month > 12 || gpsFix.day < 1 || gpsFix.day > 31)
    return;                         /* 日期非法 */

  ts = gpsUtcToTimestamp(gpsFix.year, gpsFix.month, gpsFix.day,
                         gpsFix.hour, gpsFix.minute, gpsFix.second);
  ts += 8u * 3600u;                 /* UTC → 北京时间 */

  nowTick = rtc_read_counter();     /* RTC 秒（STOP 期间连续走） */

  /* 与 SYN_TIME 首次对时一致：记录锚点，供后续晶振偏差计算 */
  anchorRtcSec    = nowTick;
  anchorTimestamp = ts;
  syncedTimestamp = ts;
  syncedRtcSec    = nowTick;
  timeSynced      = 1;

  {
    char cstBuf[20];

    /* 打印 UTC 原始时间和换算后的北京时间，两者并排避免混淆
     * （旧打印标着 UTC+8 却输出 UTC 字段，看日志容易误判成时间错了 8 小时） */
    formatTimestamp(ts, cstBuf, sizeof(cstBuf));
    printf("[对时] 卫星授时(本机首次): UTC=%04u-%02u-%02u %02u:%02u:%02u, 北京时间=%s, ts=%lu\r\n",
           gpsFix.year, gpsFix.month, gpsFix.day,
           gpsFix.hour, gpsFix.minute, gpsFix.second, cstBuf, (unsigned long)ts);
  }

  /* 对时成功后按真实时间重算下次上报时间（首次开机则立即补报一次） */
  scheduleAfterSync();
}

/**
 * @brief  阻塞等待"新鲜的有效定位"（用于 upgps 指令）
 * @note   此函数运行期间主循环不跑，必须自己反复调用 gpsTask() 排空环形缓冲，
 *         否则永远拿不到新解析结果 —— 这是本函数的核心，不能省
 * @param  minMs:     最短等待时间；即使一进来就有坐标也必须等够这么久
 *                    （给中继留够准备接收的时间，避免发太快丢帧）
 * @param  timeoutMs: 最长等待时间；HAL_GetTick 差值用无符号运算，
 *                    49 天回绕也能正确计算
 * @retval 1=拿到有效定位, 0=超时仍未定位
 */
/**
 * @brief  搜星期间的状态行（每 1 秒一行，取代原来的 NMEA 原始打印）
 * @param  elapsedMs: 本轮已搜星时长（毫秒）
 * @note   一行给出：坐标是否有效 / GPS 年份是否可信(>2025) / 定位是否可靠(新鲜) /
 *         参与定位的卫星数 / 已搜星时长 / 当前北京时间（GPS UTC + 8 小时）
 * @warning 三元里的对勾/叉号后面留一个空格：多字节字符紧贴闭合引号时
 *          ARMCC5 可能吞掉那个引号（missing closing quote）
 */
static void gpsPrintSearchStatus(uint32_t elapsedMs)
{
  uint16_t year = gpsFix.year;
  uint8_t  mon  = gpsFix.month;
  uint8_t  day  = gpsFix.day;
  uint8_t  hh   = gpsFix.hour;
  uint32_t rxTotal = 0;   /* 本轮累计收到的串口字节数 */
  uint32_t rxDelta = 0;   /* 距上次打印新增的字节数（≈ 字节/秒） */
  char     rtcBuf[24];    /* 系统(RTC)时间：未对时固定显示 1970-01-01 */

  /* RTC 系统时间：统一用 getCurrentTimestamp() 格式化。
   * 未对时(timeSynced=0)时它返回 RTC 计数（开机秒数），
   * 从 1970-01-01 00:00:00 起逐秒递增（在走，只是日期无意义）；
   * 已对时后显示真实北京时间（STOP 休眠期间 RTC 连续走时） */
  formatTimestamp(getCurrentTimestamp(), rtcBuf, sizeof(rtcBuf));

  /* GPS UTC + 8 小时 = 北京时间（跨日 / 跨月 / 跨年简单进位） */
  hh = (uint8_t)(hh + 8);
  if (hh >= 24) {
    uint8_t dim = gpsDaysInMonth[((mon >= 1) && (mon <= 12)) ? (mon - 1) : 0];

    hh = (uint8_t)(hh - 24);
    if ((mon == 2) && isLeapYear(year))
      dim = 29;
    day++;
    if (day > dim) {
      day = 1;
      mon++;
      if (mon > 12) {
        mon  = 1;
        year = (uint16_t)(year + 1);
      }
    }
  }

  /* 串口数据量：本轮累计 + 距上次打印的增量（≈ 字节/秒）。
   * 增量为 0 就说明模块没在往外吐 NMEA —— 一眼区分"天线差没定位"和"串口根本没数据" */
  {
    uint32_t rx = gpsRxCount;
    static uint32_t lastRx = 0;

    if (rx >= lastRx)
      rxDelta = rx - lastRx;
    else
      rxDelta = rx;                   /* 新一轮唤醒时 gpsPowerOn() 会把计数清零 */
    lastRx  = rx;
    rxTotal = rx;
  }

  if (gpsFixValid) {
    char latBuf[16], lngBuf[16];

    deg1e5ToStr(gpsFix.lat, latBuf);
    deg1e5ToStr(gpsFix.lng, lngBuf);
    printf("✅定位数据:%s,%s✅   年份>2025:%s   GPS可靠:%s   卫星数量:%d   搜星时间:(%lu秒)   串口数据:%lu字节(+%lu)   RTC:%s\r\n",
           latBuf, lngBuf,
           (year > 2025) ? "✅ " : "❌ ",
           gpsFixFresh() ? "✅ " : "❌ ",
           (int)gpsFix.sats,
           (unsigned long)(elapsedMs / 1000),
           (unsigned long)rxTotal, (unsigned long)rxDelta, rtcBuf);
  } else {
    printf("❌定位数据:--,--❌   年份>2025:%s   GPS可靠:%s   卫星数量:%d   搜星时间:(%lu秒)   串口数据:%lu字节(+%lu)   RTC:%s\r\n",
           (year > 2025) ? "✅ " : "❌ ",
           gpsFixFresh() ? "✅ " : "❌ ",
           (int)gpsFix.sats,
           (unsigned long)(elapsedMs / 1000),
           (unsigned long)rxTotal, (unsigned long)rxDelta, rtcBuf);
  }
}

static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs)
{
  uint32_t startTick  = HAL_GetTick();
  uint32_t elapsed;
  uint32_t lastStatus = 0;   /* 上次打印状态行的时刻（相对 startTick 的毫秒数） */

  printf("[GPS] 开始搜星: 至少 %lu 秒, 上限 %lu 秒...\r\n",
         (unsigned long)(minMs / 1000), (unsigned long)(timeoutMs / 1000));

  if (gpsPrintEn)
    gpsPrintSearchStatus(0);   /* 进入就先给一行，之后每秒刷新 */

  while (1) {
    gpsTask();                    /* 排空环形缓冲并解析 NMEA */

    elapsed = HAL_GetTick() - startTick;

    /* 每 1 秒刷新一行状态行（不再打印 NMEA 原始语句） */
    if (gpsPrintEn && ((elapsed - lastStatus) >= 1000)) {
      lastStatus = elapsed;
      gpsPrintSearchStatus(elapsed);
    }

    /* 先判"是否已满足上报条件"，再判超时 ——
     * 否则最后一刻刚好定上位，会被超时分支覆盖成失败 */
    if (elapsed >= minMs && gpsFixFresh()) {
      printf("[GPS] 已定位(第 %lu 秒), 已满足最短等待 %lu 秒, 准备上报\r\n",
             (unsigned long)(elapsed / 1000), (unsigned long)(minMs / 1000));
      return 1;
    }

    if (elapsed >= timeoutMs)
      break;

    HAL_Delay(100);
  }

  printf("[GPS] 搜星超时(%lu 秒), 未获得有效定位\r\n",
         (unsigned long)(timeoutMs / 1000));
  return 0;
}

/**
  * @brief  指令目标校验：fields[1] 是否本机设备名
  * @param  fields: 已按 '|' 切好的字段数组（fields[1]=目标, fields[2]=命令名）
  * @retval 1=是本机（执行指令）, 0=不是本机（已打印跳过原因）
  * @note   统一替代每个指令分支里重复的
  *         if (strcmp(fields[1], deviceName) == 0) { ... }
  *         else { printf("[指令] xx: 目标=... != 本机=..., 跳过"); }
  */
static uint8_t cmdIsForMe(char *fields[])
{
  if (strcmp(fields[1], deviceName) != 0) {
    printf("[指令] %s: 目标=%s != 本机=%s, 跳过\r\n",
           fields[2], fields[1], deviceName);
    return 0;
  }
  return 1;
}

/**
  * @brief  解析接收到的消息（按 '|' 分割字段）
  * @note   格式：MSG_TYPE|字段1|字段2|...|
  */
static void parse_rx_message(uint8_t *buf, uint8_t len)
{
  char *str = (char *)buf;
  char *fields[8] = {0};
  uint8_t field_count = 0;

  /* 收到任何 LoRa 数据（含乱码）都刷新链路活性 —— 哪怕解析失败，
   * 也说明中继/信道在工作，链路沉默看门狗不应对这种情况重启 */
  lastLoRaRxTs = getCurrentTimestamp();

  fields[field_count++] = str;
  for (uint8_t i = 0; i < len && field_count < 8; i++) {
    if (str[i] == '|') {
      str[i] = '\0';
      if (i + 1 < len)
        fields[field_count++] = &str[i + 1];
    }
  }

  if (field_count < 2) {
    printf("[解析] 无效消息\r\n");
    return;
  }

  uint8_t msg_type = atoi(fields[0]);

  switch (msg_type) {
    case MSG_TYPE_SYN_TIME:
      /* 4|timestamp|NN|中结名 —— 验证码算法与 CENTEN 中继端完全一致：
       *   NN = timestamp 各位数字之和的个位+十位（数位和 % 100，2 位）
       *   例: 1794048626 → 数位和 47 → "47"
       * 子设备用收到的 timestamp 重算比对，不匹配视为干扰帧/损坏帧，
       * 直接丢弃本次对时（时间、锚点、晶振偏差全都不动） */
      if (field_count >= 3) {
        uint32_t recvTs = (uint32_t)atoi(fields[1]);
        uint32_t recvCode = (uint32_t)atoi(fields[2]);
        uint32_t digitSum = 0u, t = recvTs;
        uint32_t wantCode;
        uint32_t nowTick = rtc_read_counter();  /* RTC 秒（STOP 期间连续） */

        while (t) { digitSum += t % 10u; t /= 10u; }
        wantCode = digitSum % 100u;

        /* 验证码不匹配 → 拒绝本次对时 */
        if (recvCode != wantCode) {
          printf("[对时] 码错(%02lu/%02lu), 拒绝对时\r\n",
                 (unsigned long)recvCode, (unsigned long)wantCode);
          break;
        }

        /* 无效对时防护：年份必须在 1970~2032 之间，否则视为无效对时信息，
         * 不同步本机时间（已对时保持原样，未对时继续等有效值）。
         * 覆盖场景：atoi 解析失败(=0→1970 之前按无效算不上,但 0 明显错)、
         * 下发错误值、时间戳字段损坏等 */
        if (recvTs == 0 || timestampYear(recvTs) > 2032u) {
          printf("[对时] ts=%lu 年份越界, 忽略\r\n",
                 (unsigned long)recvTs);
          break;   /* 丢弃这条对时，锚点/晶振偏差都不动 */
        }

        /* 晶振偏差计算（首次对时设为锚点，后续每次对时输出偏差）。
         * 本地间隔用 RTC 秒：STOP 休眠期间 RTC 继续走，测的才是真实钟差 */
        if (timeSynced) {
          uint32_t localElapsedSec = nowTick - anchorRtcSec;
          uint32_t remoteElapsedSec = recvTs - anchorTimestamp;
          /* 直接用间隔差值计算偏差，避免绝对时间戳整数截断 */
          int32_t  driftSec = (int32_t)localElapsedSec - (int32_t)remoteElapsedSec;
          int32_t  driftPerHour = (remoteElapsedSec > 0)
              ? (driftSec * 3600 / (int32_t)remoteElapsedSec)
              : 0;
          printf("[晶振] 偏差=%d/%lu秒=%d秒/h 本地=%lu 服务=%lu\r\n",
                 (int)driftSec,
                 (unsigned long)localElapsedSec,
                 (int)driftPerHour,
                 (unsigned long)localElapsedSec,
                 (unsigned long)remoteElapsedSec);

          /* 收敛判定（含异常剔除）：
           * ① 区间过短(<10分钟)不采样 —— 上报周期可设（含 1 分钟快速轮），
           *   区间越短测量噪声越大：1 秒对时抖动在 1 分钟区间=360秒/h，
           *   会淹没 15秒/h 收敛阈并误触剔除线；且短周期漂移累积本来就小
           *   （1 分钟才 3.6 秒），无需补偿。短区间对时仍校准时间轴、
           *   刷新锚点，只是不产生偏差样本、不动补偿值；
           * ② 偏差绝对值超 DRIFT_SAMPLE_MAX 视为中继自身授时错误，剔除；
           * ③ 连续两次可信样本差 ≤ DRIFT_CONVERGE_TOL 才启用补偿；
           * ④ 收敛后：小变化静默更新，大变化暂不采用、等下次对时确认 */
          if (localElapsedSec < 600u) {
            printf("[晶振] 区间%lu秒短, 不采样\r\n",
                   (unsigned long)localElapsedSec);
          } else if (driftPerHour > DRIFT_SAMPLE_MAX ||
                     driftPerHour < -DRIFT_SAMPLE_MAX) {
            printf("[晶振] 偏差%d秒/h异常, 剔除\r\n", (int)driftPerHour);
          } else if (!driftConverged) {
            int32_t diff = driftPerHour - lastDriftSample;

            if (diff < 0) diff = -diff;
            if (diff <= DRIFT_CONVERGE_TOL) {
              rtcDriftPerHour = driftPerHour;
              driftConverged  = 1;
              printf("[晶振] 已收敛, 启用补偿 %d秒/h\r\n", (int)rtcDriftPerHour);
              saveDriftIfChanged(rtcDriftPerHour);
            } else {
              lastDriftSample = driftPerHour;
              printf("[晶振] 采样=%d差%d 未收敛\r\n", (int)driftPerHour, (int)diff);
            }
          } else {
            int32_t diff = driftPerHour - rtcDriftPerHour;

            if (diff < 0) diff = -diff;
            if (diff <= DRIFT_CONVERGE_TOL) {
              rtcDriftPerHour = driftPerHour;   /* 静默滑动更新 */
              saveDriftIfChanged(rtcDriftPerHour);
            } else
              lastDriftSample = driftPerHour;   /* 变化大: 暂不采用, 等下次确认 */
          }
        } else {
          /* 首次对时：记录锚点 */
          anchorRtcSec = nowTick;
          anchorTimestamp = recvTs;
          printf("[晶振] 锚点已设置  ts=%lu rtc=%lu\r\n",
                 (unsigned long)recvTs, (unsigned long)nowTick);
        }

        /* 补偿残差验证：本机(含补偿)时间 vs 中绷新下发对时的差值。
         * 补偿正确时应接近 0~2 秒（链路延迟）——这就是补偿值的实时体检；
         * 且每次对时（含类型 1 GPS 轮接收窗里的对时）都会用锚点间隔
         * 重测偏差并更新补偿值，补偿值随每次对时持续趋向正确 */
        if (timeSynced) {
          int32_t syncErr = (int32_t)(getCurrentTimestamp() - recvTs);
          printf("[对时] 补偿残差=%d秒\r\n", (int)syncErr);
        }

        syncedTimestamp = recvTs;
        syncedRtcSec = nowTick;
        timeSynced = 1;

        {
          char tsBuf[24];

          /* 本机此刻的时间（含日期），对时完成后立即生效 */
          formatTimestamp(getCurrentTimestamp(), tsBuf, sizeof(tsBuf));
          printf("[对时] ts=%lu 当天秒=%lu 本机=%s\r\n",
                 (unsigned long)syncedTimestamp,
                 (unsigned long)getTodaySecond(), tsBuf);
        }
        /* 对时成功后重新计算下次上报时间（首次开机则立即补报一次） */
        scheduleAfterSync();
      }
      break;
    case MSG_TYPE_COM: /* 9: 指令  9|deviceId|cmd|value */
      if (field_count >= 3) {
        printf("[指令] %s %s %s\r\n",
               fields[1], fields[2], field_count > 3 ? fields[3] : "");
        /* realyname: 9|v3-10|realyname|v3-11  →  本机改名 */
        if (strcmp(fields[2], "realyname") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            strncpy(deviceName, fields[3], sizeof(deviceName) - 1);
            deviceName[sizeof(deviceName) - 1] = '\0';
            saveDeviceNameToFlash(deviceName);
            printf("[指令] 设备已改名: %s\r\n", deviceName);
          }
        }
        /* upgps: 9|v0-0|upgps|N  →  等待有效定位后上报 GPS 坐标
         *   N=0（或参数非数字）：单次搜星上报一次就结束
         *   N>0                ：连续定位 N 分钟，每轮至少 GPS_CONT_INTERVAL_SEC 秒
         * @note  这里只置标志，真正的等待/上报在 OnMaster() 末尾执行：
         *        1) 阻塞搜星：最短 GPS_WAIT_MIN_MS(10秒, 给中继留准备时间),
         *                    上限 GPS_WAIT_TIMEOUT_MS(4分钟)
         *        2) 首次发 5|deviceName|lat,lng|（MSG_TYPE_UP_GPS，定位失败发 "0.0,0.0"）；
         *           之后没拿到坐标的重试轮改发 1|...|（MSG_TYPE_GPS）
         *        3) calcNextTxTime() 重新算下次时间 → 再进深度休眠
         *        不能在中断/解析上下文里长时间等待，否则会堵死整个流程 */
        else if (strcmp(fields[2], "upgps") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int mins = atoi(fields[3]);        /* atoi 自动跳过前导空格 */

            if (mins < 0)
              mins = 0;
            if ((unsigned int)mins > (unsigned int)GPS_CONT_MAX_MIN) {
              mins = GPS_CONT_MAX_MIN;
              printf("[指令] upgps: 分钟数超限, 按上限 %d 处理\r\n",
                     GPS_CONT_MAX_MIN);
            }

            gpsContTotalMin = (uint16_t)mins;
            gpsWaitReport = 1;

            if (mins > 0)
              printf("[指令] upgps: 连续定位 %d 分钟, 每轮至少 %u 秒(剩余不足则提前结束)\r\n",
                     mins, (unsigned int)GPS_CONT_INTERVAL_SEC);
            else
              printf("[指令] upgps: 单次搜星上报一次(参数=0, 上限 %lu 秒)\r\n",
                     (unsigned long)(GPS_WAIT_TIMEOUT_MS / 1000));
          }
        }
        /* sendmode: 9|v0-0|sendmode|1  →  预留：原有 GPS 工作模式控制逻辑已移除
         * （GPS 电源现在由 PB15 在搜星流程里自动控制）
         * 保留本分支占位，后续在此追加新功能 */
        else if (strcmp(fields[2], "sendmode") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            /* TODO: 后续功能在此实现 */
            printf("[指令] sendmode=%d : 预留分支(当前无动作)\r\n", atoi(fields[3]));
          }
        }
        /* devicecount: 9|v0-0|devicecount|30  →  设置分时隙上报的子设备总台数
         * 时隙偏移 = 序号 / 总台数 * 上报周期，默认 30 台
         * @note 范围 1~60：周期最短 1 分钟(60秒)，60 台刚好每台 1 秒时隙
         * @note 改了台数后时隙立刻重排（每台设备各自收到指令才改），
         *       要在所有设备上广播同一条指令，否则各台的时隙划分会不一致 */
        else if (strcmp(fields[2], "devicecount") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int cnt = atoi(fields[3]);   /* atoi 会自动跳过前导空格 */

            if (cnt >= 1 && cnt <= 60) {
              deviceCount = (uint8_t)cnt;
              saveDeviceCountToFlash(deviceCount);
              printf("[指令] devicecount=%d 台, 时隙偏移改为 %lu 秒\r\n",
                     deviceCount,
                     (unsigned long)getSlotOffsetSec(
                         ((fastStartRounds > 0) && (getConfigIntervalSec() > 60u))
                             ? 60u : getConfigIntervalSec()));
            } else {
              printf("[指令] devicecount: %d 超范围(1~60), 拒绝\r\n", cnt);
            }
          }
        }
        /* readinfo: 9|v0-0|readinfo|  →  上报设备信息帧
         * 回复帧: 8|设备名|设备数量(devicecount)|发射功率(dBm)|保留(当前填计数)|
         * @note 不需要参数值，故只要求 3 段（多带值段也不影响） */
        else if (strcmp(fields[2], "readinfo") == 0 && field_count >= 3) {
          if (cmdIsForMe(fields)) {
            readInfoNeedReport = 1;
            printf("[指令] readinfo: 已标记上报设备信息\r\n");
          }
        }
        /* txpower: 9|v3-3|txpower|15  →  动态修改发射功率 */
        else if (strcmp(fields[2], "txpower") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            uint8_t newPwr = (uint8_t)atoi(fields[3]);
            currentTxPower = newPwr;
            rf_set_tx_power(newPwr);
            saveTxPowerToFlash(newPwr);
            printf("[指令] 发射功率已设置: %d dBm\r\n", newPwr);
          }
        }
        /* C: 9|v0-0|C|26.52958,109.39087  →  设置农场中心坐标并持久化 */
        else if (strcmp(fields[2], "C") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int32_t la = 0, lo = 0;
            const char *comma = strchr(fields[3], ',');
            /* 整数解析（×1e5），精度同 %.5f；parseDeg1e5 会跳过前导空格
             * （下发值形如 " 26.52,109.39"），第二段以后的内容忽略 */
            if (comma != NULL &&
                parseDeg1e5(fields[3], &la) &&
                parseDeg1e5(comma + 1, &lo) &&
                centerCoordValid(la, lo)) {
              char laBuf[16], loBuf[16];
              /* 归一化成 5 位小数存储：帧长固定，且天然不含会破坏帧结构的 '|' */
              deg1e5ToStr(la, laBuf);
              deg1e5ToStr(lo, loBuf);
              snprintf(centerStr, sizeof(centerStr), "%s,%s", laBuf, loBuf);
              saveCenterToFlash(centerStr);
              printf("[指令] 中心坐标已保存: %s\r\n", centerStr);
            } else {
              /* 非法：不写内存、不写 Flash，保留上一次的合法值 */
              printf("[指令] C: 坐标非法已拒绝(原值=%s 保持): %s\r\n",
                     (centerStr[0] != '\0') ? centerStr : "(未设置)", fields[3]);
            }
            /* 无论本次是否合法都上报当前坐标：
             * 合法→回新坐标；非法→回上一个合法值（未设过则 "0,0"） */
            centerNeedReport = 1;
          }
        }
        /* A: 9|v3-17|A|10,0M,3t,6  →  更新配置并标记上报 */
        else if (strcmp(fields[2], "A") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            /* 合法性检查 1：时间段字段（第 2/3 段）必须是 2 位 base62 字符
             * （LoRa 乱码防护：位数不对或字符越界一律拒绝，不写入 Flash） */
            char wf[8], gf[8];

            getConfigFieldStrFrom(fields[3], 1, wf, sizeof(wf));
            getConfigFieldStrFrom(fields[3], 2, gf, sizeof(gf));

            /* 合法性检查 2：上报周期必须在 1~60 分钟之间 */
            uint32_t newMin = (uint32_t)atoi(fields[3]);

            if (!configTimeFieldValid(wf) || !configTimeFieldValid(gf)) {
              /* 中文值不写三元表达式（ARMCC5 GBK 吞引号），用 if/else */
              printf("[指令] A: 时间段字段非法, 应为2位base62, 拒绝\r\n");
              printf("[指令]   工作时段=\"%s\"  GPS时段=\"%s\"\r\n", wf, gf);
            } else if (newMin >= 1 && newMin <= 60) {
              strncpy(configStr, fields[3], sizeof(configStr) - 1);
              configStr[sizeof(configStr) - 1] = '\0';
              saveConfigToFlash(configStr);
              configNeedReport = 1;  /* 标记需要上报配置 */
              printf("[指令] 配置已更新: %s\r\n", configStr);

              /* 工作时间段 / GPS 上报时间段都是从 configStr 实时解析的，
               * 这里不用做额外的"重新加载"，直接把生效的区间打出来核对 */
              printTimeWindows("[指令] 新时段 : ");
            } else {
              printf("[指令] A: 间隔=%lu 超范围(1~60), 拒绝\r\n",
                     (unsigned long)newMin);
            }
          }
        }
        /* relayreboot: 9|v0-0|relayreboot|  →  远程重启设备
         * @note NVIC_SystemReset() 真复位（等同断电重启）：
         *       RAM 全部清零（fastStartRounds/对时状态/重试标志都回到初始），
         *       不是 STOP 深度休眠那种"睡一觉继续"，重新走完整开机流程：
         *       快速上报 3 轮 → 等对时 → 恢复正常调度 */
        else if (strcmp(fields[2], "relayreboot") == 0) {
          if (cmdIsForMe(fields)) {
            printf("[指令] relayreboot, 即将重启\r\n");
            HAL_Delay(100);            /* 让日志从串口发完 */
            NVIC_SystemReset();
          }
        } else {
          printf("[指令] 未知指令: %s\r\n", fields[2]);
        }
      }
      break;
    default:
      printf("[消息] 类型=%d\r\n", msg_type);
      break;
  }
}

/**
  * @brief  Master（主机/发射端）流程
  * @note   1. 组包并发送时间帧 MSG_TYPE_TIME|设备名|当天秒数|电量|计数|
  *            @warning 这里固定发时间帧，不再按定位状态切换！
  *                    定位坐标统一由 UPGPS 流程上报（见末段 gpsSearchAndReport）
  *            例外：若上一轮搜星失败（发了 0.0,0.0），本轮跳过这一帧，
  *                  直接重试搜星，把这次机会留给拿坐标
  *         2. 发送完成后开启 3 秒接收窗口，解析对时消息
  *            @note  接收窗口两种周期都开，保证节点始终可被下发指令
  *         3. 计算下次上报时间并保存到 Flash
  */

/**
  * @brief  开启 3 秒接收窗口（single 模式，最多只收一条），收到则解析
  * @note   抽成函数的原因：MSG_TYPE_GPS(1) 上报之后还要再开一次窗口，
  *         保证节点报完定位后仍处于可被下发指令的状态；
  *         而 MSG_TYPE_UP_GPS(5) 的首次跟踪上报后不再开窗口
  */
static void openRxWindow3000(void)
{
  rf_enter_single_timeout_rx(3000);
  printf("[接收窗口] 3秒监听中...\r\n");

  /* 等待接收完成或超时 */
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

    if (local_size < RX_LEN)
      local_buf[local_size] = '\0';
    else
      local_buf[RX_LEN - 1] = '\0';

    printf("[接收] len=%d rssi=%d snr=%d data=%s\r\n",
           local_size, local_rssi - 256, local_snr, (char *)local_buf);

    /* 解析消息 */
    parse_rx_message(local_buf, local_size);
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[接收窗口] 超时, 无数据\r\n");
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    printf("[接收窗口] CRC校验错误\r\n");
  }
}

/**
  * @brief  把待回的包发出去（配置帧 / 农场中心坐标帧）
  * @note   收到 A(配置) 或 C(中心坐标) 指令后只是置位标记，真正回包在这里做。
  *         要在"每个接收窗口之后"都调用一次：普通轮的窗口在定时帧之后、
  *         GPS 轮的窗口在 MSG_TYPE_GPS 之后，位置不同；
  *         否则末尾窗口里收到的设置指令要拖到下一轮才回包
  */
static void reportPendingMessages(void)
{
  /* 配置需要上报时，延迟 1 秒发送配置消息（开机首次或收到配置更新指令） */
  if (configNeedReport) {
    HAL_Delay(1000);
    sendConfigMessage();
    configNeedReport = 0;  /* 标记已上报 */
  }

  /* 收到 readinfo 指令后，延迟 1 秒上报设备信息 8|deviceName|台数|功率|保留| */
  if (readInfoNeedReport) {
    HAL_Delay(1000);
    sendReadInfoMessage();
    readInfoNeedReport = 0;  /* 标记已上报 */
  }

  /* 收到 C 指令后，延迟 1 秒上报农场中心坐标 1|deviceName|lat,lng| */
  if (centerNeedReport) {
    HAL_Delay(1000);
    sendCenterMessage();
    centerNeedReport = 0;  /* 标记已上报 */
  }
}

static void OnMaster(void)
{
  char tx_msg[TX_LEN];
  uint32_t today_sec = getTodaySecond();
  uint32_t tx_time = 1000;

  /* 上轮失败的重试只在"工作时间段内"继续：已离开工作时间段就放弃重试，
   * 否则会出现"跳过定时帧、又不搜星"的空转轮次，设备整轮什么都不上报
   * @note 未对时(timeSynced=0)时两个时段判断都恒为 0，此时**不能**放弃重试：
   *       否则一开机没对时就会把重试标记清掉，永远拿不到坐标。
   *       未对时一律继续重试，等到对上时间后再按时段判定 */
  if (gpsNeedRetry && timeSynced && !isInWorkWindow() && !isInGpsWorkWindow()) {
    gpsNeedRetry = 0;
    upgpsRetryOneShot = 0;
    printf("[调度] 离开工作时段, 放弃重试\r\n");
  }

  /* 下列两种情况不发定时帧 MSG_TYPE_TIME，把这一轮让给 GPS：
   *   1) 上轮无坐标要重试(gpsNeedRetry)
   *   2) 当前在 GPS 工作时间段内
   *      —— 本轮改由末尾的 MSG_TYPE_GPS 上报，并在它上报后开接收窗口，
   *         对时与接收下发指令都在那个窗口完成（故本处不再开窗口）
   * @note 本轮已收到 upgps 指令(gpsWaitReport=1)时不跳过：末尾走的是 UP_GPS
   *       老逻辑（上报后不开窗口），必须靠定时后这个窗口收指令/对时 */
  uint8_t skipTimeFrame =
      ((gpsNeedRetry || isInGpsWorkWindow()) && !gpsWaitReport) ? 1U : 0U;

  if (skipTimeFrame) {
    if (gpsNeedRetry)
      printf("[调度] 上轮无坐标, 本轮GPS重试\r\n");
    else
      printf("[调度] GPS时段内, 本轮走GPS帧\r\n");
  } else {
    /* 组包：2|v0-0|当天秒数|电量|计数
     * 周期上报固定发时间帧，不管有没有定位 —— 定位由 UPGPS 流程单独触发上报 */
    txCountNext();   /* 每发一帧 LoRa 计数 +1 */
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%lu|%d|%lu",
             MSG_TYPE_TIME, deviceName,
             (unsigned long)today_sec, cachedBatteryPercent, (unsigned long)tx_count);

    /* 发送数据 */
    rf_single_tx_data((uint8_t *)tx_msg, strlen(tx_msg) + 1, &tx_time);

    /* 等待发送完成标志 */
    while (rf_get_transmit_flag() == RADIO_FLAG_IDLE)
      ;
    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    printf("发送成功 -> \"%s\"\r\n", tx_msg);
  }

  /* ---- 3 秒接收窗口（single 模式，最多只收一条） ----
   * 只在本轮发了定时帧时在这里开；跳过定时帧的轮次改为在末尾 MSG_TYPE_GPS
   * 上报之后再开（见下面 GPS 块），避免"没发帧还白等 3 秒" */
  if (!skipTimeFrame)
    openRxWindow3000();

  /* 窗口里收到的 A/C 等设置指令，本轮就回包 */
  reportPendingMessages();

  /* ---- GPS 上报流程 ----
   * 规则（GPS 工作时间段一定落在工作时间段之内）:
   *   (1) upgps 指令(gpsWaitReport) —— 任何时间收到都立刻搜星，
   *       用 MSG_TYPE_UP_GPS(5) 上报，上报后**不开**接收窗口
   *   (2) 上一次 MSG_TYPE_UP_GPS 没拿到正确坐标(gpsNeedRetry) ——
   *       之后每个上报点都用 MSG_TYPE_GPS(1) 重试并开接收窗口，
   *       即使当前不是 GPS 工作时间也一样，直到拿到正确坐标为止
   *       （仅在工作时间段内重试，离开工作时间段则放弃，见 OnMaster 开头）
   *   (3) GPS 工作时间段(config 第 3 段) 内 —— 正常用 MSG_TYPE_GPS(1) 上报
   *       并开接收窗口；已有新鲜坐标就直接发缓存坐标，没有坐标才临时搜星
   * 放在 calcNextTxTime() 之前 —— 等待可能耗时几十秒，
   * 之后必须重新计算下次上报时间，不能用本轮算出的旧值。 */
  {
    uint8_t inGpsWin = isInGpsWorkWindow();

    if (gpsWaitReport || gpsNeedRetry || inGpsWin) {
      uint8_t firstCmd = gpsWaitReport;        /* upgps 指令触发的第一次 */
      uint8_t useType1 = firstCmd ? 0U : 1U;   /* 0=UP_GPS(5), 1=GPS(1) */

      gpsWaitReport = 0;  /* 先清零，避免异常路径残留导致每轮都在这儿等 */

      if (firstCmd)
        printf("[GPS] upgps首次: 类型5不开窗\r\n");
      else
        printf("[GPS] 类型1上报, 后开窗\r\n");

      /* 类型5(upgps) 不受时间约束：一律现场搜星，搜完再发（时间可后移）。
       * 万一有残留的预搜标志（例如上一轮被指令打断），在这里清掉，避免误用 */
      if (firstCmd && gpsPreSearched) {
        gpsPreSearched = 0;
        gpsPreFixOk    = 0;
        printf("[GPS] 忽略预搜,现场搜\r\n");
      }

      if (gpsPreSearched) {
        /* 【提前搜星】本轮在唤醒时已预先搜完（搜到 或 窗口不足被跳过）：
         * 直接取结果上报，绝不再现场搜星 —— 上报时间点因此不会后移。
         * 预搜成功后 GPS 已断电，坐标暂存在 gpsFix 里（gpsPowerOff 不清坐标） */
        uint8_t preOk = gpsPreFixOk;

        /* 中文值不要写进三元表达式：ARMCC5 按 GBK 解析会吞掉引号 */
        if (preOk)
          printf("[GPS] 预搜: 已定位(%lus)\r\n", (unsigned long)gpsPreSec);
        else
          printf("[GPS] 预搜: 未定位(%lus)\r\n", (unsigned long)gpsPreSec);

        sendGpsPositionMessage(preOk, useType1, gpsPreSec, gpsPreBytes);
        gpsNeedRetry = preOk ? 0U : 1U;
        if (upgpsRetryOneShot) {
          /* UPGPS 结束后的一次性重试已用掉：这次仍无坐标就不再延续 */
          upgpsRetryOneShot = 0;
          if (gpsNeedRetry) {
            gpsNeedRetry = 0;
            printf("[GPS] 一次性重试用完\r\n");
          }
        }

        gpsPreSearched = 0;
        gpsPreFixOk    = 0;
        gpsPowerOff();   /* 确保断电（预搜里已断电，重复调用安全） */
      } else {
        uint8_t hasFix;

        /* 兜底路径（未走预搜，如运行中被现场触发）：
         * 这一轮要用 GPS：PB15 置低上电，用完立刻置高断电
         * @note 一律重新搜星，不发缓存坐标：
         *       缓存里的坐标可能是很久之前某一轮拿到的，不能代表当前位置，
         *       所以即使 gpsFixFresh() 为真也要重新搜一遍再上报 */
        gpsPowerOn();

        /* GPS 打印的开关与恢复由 gpsSearchAndReport() 内部逐轮处理 */
        if (gpsContTotalMin > 0) {
          /* 连续定位模式（upgps|N 跟踪）：时长内不休眠，
           * 每 GPS_CONT_INTERVAL_SEC 秒上报一次坐标；
           * 无坐标时跟踪期限自动保底延长到 60 分钟（定位成功导向）。
           * 跟踪结束：成功 → 恢复定时上报；失败 → 只保留**一次**
           * 类型 1 重试机会（下轮对时点搜星上报一次），这次仍无坐标
           * 就彻底清零不再延续 —— 60 分钟+1 次重试都失败说明环境
           * 真无信号，无限延续只是每轮白烧 4 分钟搜星电 */
          uint8_t contOk = gpsRunContinuous(gpsContTotalMin, useType1);

          gpsContTotalMin = 0;
          gpsNeedRetry = contOk ? 0U : 1U;
          upgpsRetryOneShot = contOk ? 0U : 1U;
          if (contOk)
            printf("[GPS] 跟踪结束\r\n");
          else
            printf("[GPS] 跟踪结束,留1次重试\r\n");
        } else {
          /* 单次搜星上报（参数为 0 的 upgps 指令，或失败后的自动重试） */
          hasFix = gpsSearchAndReport(useType1);
          gpsNeedRetry = hasFix ? 0U : 1U;
          if (upgpsRetryOneShot) {
            /* UPGPS 结束后的一次性重试已用掉：这次仍无坐标就不再延续 */
            upgpsRetryOneShot = 0;
            if (gpsNeedRetry) {
              gpsNeedRetry = 0;
              printf("[GPS] 一次性重试用完\r\n");
            }
          }
        }

        /* GPS 用完：PB15 置高断电
         * （连续定位模式是整段结束才断电，避免反复上电/断电） */
        gpsPowerOff();
      }

      /* MSG_TYPE_GPS(1) 上报后开接收窗口（对时 + 收下发指令）；
       * MSG_TYPE_UP_GPS(5) 的首次跟踪上报不开窗口，保持老逻辑 */
      if (useType1) {
        openRxWindow3000();

        /* 这个窗口里收到的指令照常处理：upgps → 置 gpsWaitReport 下一轮执行；
         * A/C 等设置 → 本轮立即回包，不等下一轮 */
        reportPendingMessages();
      }

      /* 注意：分支提示不要写成 one-liner 三元 + 中文值，
       * 中文紧贴闭合引号会被 ARMCC5 按 GBK 吞掉那个引号（missing closing quote），
       * 故改成 if/else + 以 \r\n 收尾（与文件其它 printf 风格一致） */
      if (gpsNeedRetry)
        printf("[调度] 下轮继续重试搜星(跳过定时上报)\r\n");
      else
        printf("[调度] 下轮恢复正常定时上报\r\n");
    }
  }

  /* 开机前 5 轮快速上报：本轮已上报完成，消耗一次配额
   * （先递减再算下次时间：轮1~轮5 都是 5 分钟，轮6 起恢复配置周期） */
  if (fastStartRounds > 0)
    fastStartRounds--;

  /* 计算下次上报时间（配置更新后间隔可能变化） */
  calcNextTxTime();

  /* 判断是否需要进入深度休眠 */
  {
    uint32_t nowTs = getCurrentTimestamp();
    uint32_t waitSec = (nextTxTimestamp > nowTs) ? (nextTxTimestamp - nowTs) : 0;
    char     nextBuf[24];

    /* 下次发送的绝对时间点（HH:MM:SS）。
     * 格式化结果是 "YYYY-MM-DD HH:MM:SS"，+11 直接指向时分秒；
     * 未对时时是 1970 起递增的本机时钟，一样能看出节奏 */
    formatTimestamp(nextTxTimestamp, nextBuf, sizeof(nextBuf));

    if (waitSec > 10) {
      /* 下次上报 > 10 秒后 → PAN3029 进入深度休眠省电 */
      printf("[休眠] 深睡%lu分%lu秒后, %s\r\n",
             (unsigned long)(waitSec / 60u), (unsigned long)(waitSec % 60u),
             nextBuf + 11);
      rf_deepsleep();
      loraInitialized = 0;  /* 标记需要唤醒后重新初始化 */
    } else {
      printf("[活跃] LoRa保持就绪...下次发送 %lu分%lu秒后, %s\r\n",
             (unsigned long)(waitSec / 60u), (unsigned long)(waitSec % 60u),
             nextBuf + 11);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  应用程序主入口函数
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有配置的外设 */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_NVIC_Init();
  MX_ADC1_Init();          /* 电池电量检测：PA0 / ADC1_CH0 */

  /* USER CODE BEGIN 2 */

  /* RTC：STOP 深度休眠的时间基准与唤醒源（LSE 优先，无晶振退 LSI） */
  rtc_init();

  /* 从 Flash 加载设备名、配置、发射功率、农场中心坐标和设备台数 */
  loadDeviceNameFromFlash();
  loadDriftFromFlash();
  loadConfigFromFlash();
  loadTxPowerFromFlash();
  loadCenterFromFlash();
  loadDeviceCountFromFlash();

  /* 打印启动信息 */
  printf("\r\n===== LoRa 仅发送模式启动 =====\r\n");
  printf_logo();
  printf("设备: %s\r\n", deviceName);
  printf("配置: %s\r\n", configStr);
  printf("时隙  : 序号=%d, 总台数=%d, 间隔=%lu秒, 偏移=%lu秒\r\n",
         getDeviceIndex(), deviceCount,
         (unsigned long)getConfigIntervalSec(),
         (unsigned long)getSlotOffsetSec(getConfigIntervalSec()));
  printTimeWindows("时段  : ");   /* 工作时间段 + GPS 上报时间段 */

  /* 首次 TX 延迟到主循环中执行（LoRa 在首次 TX 前才初始化） */
  printf("TX模式: 仅发送\r\n");
  nextTxTimestamp = 0;  /* 首次立即触发 TX */
  printf("[调度] 就绪, 首次立即发送...\r\n");

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */
    uint32_t nowTs = getCurrentTimestamp();

    if (loraInitialized) {
      /* 链路沉默看门狗：工作时间内 2 小时没收到任何 LoRa 信息 → 真复位。
       * 活跃期间每圈(100ms)查一次，开销可忽略；深度休眠期间 RTC 时间戳
       * 照样连续计时，醒来必过这里，所以休眠多久都不会漏检 */
      linkSilenceCheck();

      /* LoRa 已激活：检查是否到达发送时间 */
      if (nowTs >= nextTxTimestamp) {
        OnMaster();  /* 内部会处理发送、接收窗口、以及发送后的休眠判断 */
      }
    } else {
      /* LoRa 未初始化（首次启动 或 从 deepsleep 唤醒）
       * 唤醒时刻按"本轮是否必须准点的 GPS 轮"决定：
       *   类型1(GPS时段/重试) → 提前搜星，到点准点发帧（提前量见下）
       *   类型5(upgps 指令)   → 提前 5 秒醒，醒后现场搜星，上报时间可后移
       *   普通定时帧          → 提前 5 秒醒（LoRa 初始化倒计时）
       *
       * 提前量 = min(搜星上限+5, 周期-5)：
       *   正式上线周期(30~60 分钟) > 搜星上限，取 245 秒，搜星时间充足；
       *   若周期被设得很小(如 1 分钟 < 4 分钟搜星上限)，压缩为"周期-5 秒"，
       *   保证提前量不跨进上一轮，绝不会把这个上报点挤掉；
       *   压缩后窗口不足 GPS_WAIT_MIN_MS(10 秒) 时 gpsPreSearch() 会跳过搜星，
       *   到点照发 "0.0,0.0|字节数|" —— 准点优先，绝不后移 */
      uint8_t  timelyGps   = gpsNeededTimelyAt(nextTxTimestamp);
      uint8_t  doPreSearch = (timelyGps && !gpsPreSearched && !gpsContTotalMin) ? 1U : 0U;
      uint32_t leadSec     = 5u;
      uint32_t wakeTime;

      if (timelyGps) {
        uint32_t intervalSec = getConfigIntervalSec();
        uint32_t maxLead = (intervalSec > 10u) ? (intervalSec - 5u) : 5u;

        leadSec = (GPS_PRESEARCH_LEAD_SEC < maxLead) ? GPS_PRESEARCH_LEAD_SEC : maxLead;
        if (leadSec < 5u)
          leadSec = 5u;
      }
      wakeTime = (nextTxTimestamp > leadSec) ? (nextTxTimestamp - leadSec) : 0u;

      if (nowTs >= wakeTime) {
        /* STOP 醒来时钟回落 HSI 8MHz，先恢复 72MHz（SysTick 随之恢复） */
        SystemClock_Config();
        /* 唤醒 LoRa 并执行 5 秒倒计时 */
        printf("[WAKE] 5秒倒计时\r\n");
        loraInitFirstTime();
        /* 读取电量并缓存（每轮上报前刷新一次，上报帧里带的就是它） */
        cachedBatteryPercent = GetBatteryPercent();
        for (int i = 5; i > 0; i--) {
          char time_str[20];
          formatTimestamp(getCurrentTimestamp(), time_str, sizeof(time_str));
          printf("[WAKE] %s  %d\r\n", time_str, i);
          HAL_Delay(1000);
        }

        /* 需要 GPS：醒后立刻预搜（搜星窗口到"上报点前 5 秒"截止），
         * 保证到点无论有无坐标都能准时发帧 */
        if (doPreSearch) {
          if (leadSec < GPS_PRESEARCH_LEAD_SEC)
            printf("[GPS] 上报周期小于搜星上限: 提前量压缩为 %lu 秒, 尽力搜星(到点必发)\r\n",
                   (unsigned long)leadSec);

          gpsPreSearch();

          {
            uint32_t now2  = getCurrentTimestamp();
            uint32_t wake2 = (nextTxTimestamp > 5u) ? (nextTxTimestamp - 5u) : now2;

            if (wake2 > now2 + 5u) {
              /* 距离上报点还有 >5 秒：GPS 已断电，进 STOP 睡到点前 5 秒。
               * 醒来只需初始化 LoRa + 倒计时，到点直接发预搜结果（最省电） */
              printf("[调度] 预搜完成, 距上报还有 %lu 秒, 再休眠到点前 5 秒\r\n",
                     (unsigned long)(wake2 - now2));
              rf_deepsleep();
              loraInitialized = 0;
              enterStopUntil(wake2);
            }
          }
        }
      } else if (!gpsContTotalMin && !gpsPreSearched) {
        /* 等待期：MCU 进 STOP 深度休眠，RTC 闹钟在 wakeTime 唤醒；RAM 内容保持。
         * 说明：
         *   - 类型5(upgps) 允许睡到"上报点前 5 秒"，醒后现场搜星再发（不受约束）
         *   - 类型1 允许睡到"上报点前 提前量"，醒后先预搜再到点发（准点）
         *   所以 GPS 待办不再需要空转等满一个周期（省电）
         * 双保险：确认 LoRa 已深度休眠（OnMaster 末尾已调过，重复调用安全） */
        rf_deepsleep();
        enterStopUntil(wakeTime);
      }
      /* 连续定位模式(gpsContTotalMin)保持活跃空转，由 HAL_Delay(100) 节流 */
    }

    /* GPS: 取出环形缓冲里的 NMEA 语句并打印/解析 */
    gpsTask();

    HAL_Delay(100);

    /* USER CODE END 3 */
  }
  /* USER CODE END WHILE */
}

/**
  * @brief System Clock Configuration (72MHz)
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function (调试串口 115200)
  */
static void MX_USART1_UART_Init(void)
{
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
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */
}

/**
  * @brief USART3 Initialization Function (GPS 模块 9600, 仅接收)
  * @note  PB11 = USART3_RX（PB10/TX 由 HAL_UART_MspInit 一并配置，但本模式不用）
  *        中断优先级设为 2，低于 LoRa(EXTI2=0)，避免影响射频时序
  */
static void MX_USART3_UART_Init(void)
{
  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);

  /* 开启 GPS 逐字节中断接收 */
  HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);

  /* USER CODE END USART3_Init 2 */
}

/**
  * @brief  GPIO 初始化函数
  * @note   配置说明：
  *         1. LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1
  *         2. LoRa 控制引脚：CSN(PB0), RST(PA1)
  *         3. LoRa 中断引脚：IRQ(PA2) - 上升沿触发，下拉
  *         4. PB15：控制引脚，推挽输出，保持低电平
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* PB15 = GPS 电源开关：推挽输出，默认高电平 = GPS 断电（极性: 低=上电）。
   * 搜星时 gpsPowerOn() 拉低上电，搜完 gpsPowerOff() 拉高断电 */
  GPIO_InitStruct.Pin   = GPS_PWR_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_PWR_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* GNSS 唤醒脚 (PB1 → 模块 WAKE_UP)：搜星状态同步控制
   *   搜星时   gpsPowerOn()  拉高 → Full on（先拉高再上电，见该函数时序说明）
   *   非搜星时 gpsPowerOff() 拉低 → Standby(20uA)，RTC 域保持可热启动
   * 初始为低：开机即"非搜星"状态（此时 PB15 也是断电，低电平无副作用；
   * 若模块因硬件过渡期常供电，则进 Standby 省电，等第一次搜星拉高唤醒） */
  /* GPS 复位引脚 (PA3 → L76K RESET_N) - 平时高电平(不复位) */
  GPIO_InitStruct.Pin   = GPS_RST_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_RST_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin   = GNSS_WAKE_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GNSS_WAKE_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET); /* 低 = Standby */

  /* LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LoRa CSN 片选引脚 (PB0) - 默认高电平 */
  GPIO_InitStruct.Pin  = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);

  /* LoRa RST 复位引脚 (PA1) - 默认高电平 */
  GPIO_InitStruct.Pin  = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);

  /* LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
  * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI）
  * @note   CPOL=0, CPHA=0, MSB, 软件 NSS, 波特率 4.5MHz
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  ADC1 初始化（电池电量检测 PA0/ADC1_CH0，寄存器方式）
  * @note   不依赖 stm32f1xx_hal_adc.c，直接操作寄存器
  *         硬件：电池 → 100K → PA0 → 100K → GND（1:1 分压）
  */
static void MX_ADC1_Init(void)
{
  uint32_t timeout;

  /* 1. 先设置 ADC 时钟分频 /8（必须在 ADC 时钟使能之前） */
  RCC->CFGR &= ~(3u << 14);
  RCC->CFGR |=  (3u << 14);               /* ADCPRE = /8, 9MHz */

  /* 2. 使能 GPIOA 和 ADC1 时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  RCC->APB2ENR |= (1u << 9);              /* ADC1EN = 1 */
  (void)RCC->APB2ENR;

  /* 3. 复位 ADC1 外设 */
  RCC->APB2RSTR |=  (1u << 9);
  RCC->APB2RSTR &= ~(1u << 9);

  /* 4. 配置寄存器：单通道单次转换，CH0(PA0)，最长采样时间 */
  ADC1->CR1 = 0;
  ADC1->CR2 = 0;
  ADC1->SQR1 = 0;                         /* 1 个转换 */
  ADC1->SQR2 = 0;
  ADC1->SQR3 = 0;                         /* CH0 (PA0) */
  ADC1->SMPR2 = (7u << 0);                /* CH0 采样 239.5 周期 */

  /* 5. 上电 ADC */
  ADC1->CR2 = ADC_CR2_ADON;
  HAL_Delay(10);

  /* 6. 校准（STM32F1 必须校准才能正常工作） */
  ADC1->CR2 |= ADC_CR2_RSTCAL;
  timeout = 1000000;
  while ((ADC1->CR2 & ADC_CR2_RSTCAL) && --timeout);
  ADC1->CR2 |= ADC_CR2_CAL;
  timeout = 1000000;
  while ((ADC1->CR2 & ADC_CR2_CAL) && --timeout);

  printf("[ADC] 校准完成 CR2=0x%08lX\r\n", (unsigned long)ADC1->CR2);

  /* 7. 空读一次：确认 ADON 触发 + EOC 等待这条链路通，结果丢弃 */
  (void)ADC1_Read();
}

/**
  * @brief  NVIC 中断控制器初始化（LoRa IRQ EXTI2）
  */
static void MX_NVIC_Init(void)
{
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/* USER CODE BEGIN 4 */

/**
  * @brief  ADC1 单次读取（寄存器方式）
  * @return 12 位 ADC 值（0~4095）
  */
static uint16_t ADC1_Read(void)
{
  uint32_t timeout = 1000000;
  /* STM32F1: 设置 ADON 触发转换（比 SWSTART 更可靠） */
  ADC1->CR2 |= ADC_CR2_ADON;
  while (!(ADC1->SR & ADC_SR_EOC) && --timeout);
  return (uint16_t)(ADC1->DR & 0xFFF);
}

/**
  * @brief  获取电池电量百分比（0~99）
  * @note   PA0 接分压电路（100K+100K），V_bat = ADC值/4096 * 3.3V * 2
  *         锂电池映射：4.2V=99%, 3.3V=0%，分段线性插值
  */
static uint8_t GetBatteryPercent(void)
{
  uint32_t adc_sum = 0;
  uint8_t i;

  /* 连续采样 8 次取平均，抑制噪声 */
  for (i = 0; i < 8; i++) {
    adc_sum += ADC1_Read();
  }
  uint16_t adc_val = adc_sum / 8;

  /* 计算电池电压：分压比 1:1，基准 3.3V */
  /* V_bat(mV) = adc_val * 3300 * 2 / 4096 = adc_val * 6600 / 4096 */
  uint32_t bat_mv = (uint32_t)adc_val * 6600 / 4096;

  /* 锂电池电压 → 百分比 分段线性映射 */
  uint8_t percent;
  if (bat_mv >= 4200)      percent = 99;
  else if (bat_mv >= 4000) percent = 80 + (bat_mv - 4000) * 19 / 200;
  else if (bat_mv >= 3700) percent = 50 + (bat_mv - 3700) * 30 / 300;
  else if (bat_mv >= 3500) percent = 20 + (bat_mv - 3500) * 30 / 200;
  else if (bat_mv >= 3300) percent = (bat_mv - 3300) * 20 / 200;
  else                     percent = 0;

  if (percent > 99) percent = 99;

  printf("[电量] ADC=%d V=%lu.%01luV => %d%%\r\n",
         adc_val, (unsigned long)(bat_mv / 1000), (unsigned long)((bat_mv / 100) % 10), percent);

  return percent;
}

/**
  * @brief  GPIO 外部中断回调函数
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* LoRa IRQ 中断: PA2 */
    if (GPIO_Pin == GPIO_PIN_2)
    {
        rf_irq_process();
    }
}

/**
 * @brief  USART3 接收完成回调（GPS 逐字节接收）
 * @note   只做两件最轻的动作：入环形缓冲 + 重新开启下一字节接收。
 *         绝不在 ISR 里做字符串解析或 printf —— 那样会长时间占用中断。
 *         缓冲满时丢弃本次字节，下一条语句的 '\n' 会自动重新同步。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    uint16_t next = (gpsWr + 1u) % GPS_RING_LEN;

    if (next != gpsRd)          /* 环形缓冲未满 */
    {
      gpsRing[gpsWr] = gpsByte;
      gpsWr = next;
      gpsRxCount++;
    }

    HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);
  }
}

/**
 * @brief  GPS 上电：PB15 置低 → 模块开机开始搜星
 * @note   1) 电源是冷启动（不使用 WAKE_UP 休眠模式），上电到输出 NMEA
 *            需要 GPS_PWR_ON_DELAY_MS（断电重启比 Standby 唤醒更久）
 *         2) 清掉环形缓冲里可能残留的半截语句，避免解析出错
 *         3) 每次上电模块时钟域都是冷的，首次定位时间比热启动长，属正常
 */
static void gpsPowerOn(void)
{
  gpsRd = gpsWr;      /* 丢弃残留字节（关中断非必需：ISR 只改 gpsWr） */
  gpsLineLen = 0;
  gpsRxCount = 0;     /* 本轮重新统计串口数据量，便于观察模块有没有在吐数据 */

#if GPS_PWR_CTRL_EN
  /* 顺序关键：先把 WAKE_UP(PB1) 拉高，再上电 ——
   * 若上电瞬间 WAKE_UP 为低，模块会直接进 Standby 而不搜星。
   * 两种起始状态都覆盖：
   *   - 模块此前被断电(PB15=高)：上电时读到 WAKE_UP=高 → 直接 Full on；
   *   - 模块此前在 Standby(PB1=低保持)：拉高即完成唤醒（热启动 ~2 秒） */
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_SET); /* 高 = Full on */
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_RESET);   /* 低 = 上电 */
  HAL_Delay(GPS_PWR_ON_DELAY_MS);
  gpsPowerState = 1;
  printf("[GPS] Power ON (PB15=低上电, PB1=高 Full on)\r\n");
#else
  gpsPowerState = 1;   /* 核对阶段：PB15 未驱动，模块由外部电源供电 */
  printf("[GPS] 开始搜星(核对阶段: PB15 未驱动, 由外部电源决定模块状态)\r\n");
#endif
}

/**
 * @brief  GPS 断电：PB15 置高 → 模块完全关机
 * @note   断电后旧坐标立即失效：下一轮不能把上次的定位当成有效坐标上报，
 *         否则 GPS_FIX_TTL_MS 内会误判成"已定位"而跳过真正的新一轮搜星
 */
static void gpsPowerOff(void)
{
#if GPS_PWR_CTRL_EN
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);     /* 高 = 断电 */
  /* WAKE_UP(PB1) 拉低 → 模块进 Standby(20uA)：
   *   - 若模块已断电：低电平被保持，下次上电前 PowerOn 会先拉高；
   *   - 若模块仍供电（硬件过渡期常供电）：进入 Standby 低功耗待机，
   *     RTC 域保持，下次拉高即热启动(~2 秒)，比冷启动省大量搜星电流 */
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET); /* 低 = Standby */
#endif

  gpsFixValid = 0;
  gpsPowerState = 0;   /* 断电后串口数据一律在 gpsTask 里丢弃 */
  gpsRd = gpsWr;
  gpsLineLen = 0;

#if GPS_PWR_CTRL_EN
  printf("[GPS] Power OFF (PB15=高断电, PB1=低 Standby)\r\n");
#else
  printf("[GPS] 本轮搜星结束(核对阶段: PB15 未驱动, 模块状态不变)\r\n");
#endif
}

/* USER CODE END 4 */

/**
  * @brief  错误处理函数
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
