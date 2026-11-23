/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PAN3029 LoRa 定时上报节点 - STM32F103C8T6
  *                   移植�?PAN3029_F030 上报逻辑
  * @note           功能说明�?
  *                 1. TX 模式：每隔一段时间上报一�?MSG_TYPE_TIME|设备名|当天秒数|电量|计数|
                       （固定发时间帧；GPS 坐标�?upgps 指令单独触发上报�?
  *                 2. 发送后开�?3 �?RX 窗口，解析对时消息（MSG_TYPE_SYN_TIME�?
  *                 3. 调试输出：通过 USART1 输出系统状�?
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
#include "reliability.h"
#include "nvstore.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEMO_VER     "V2.2"
#define GPS_MIN_BATTERY_PCT 30u /* below this: GPS off; LoRa remains available */

#define TX_LEN       64
#define RX_LEN       64

/* 消息类型定义（与 PAN3029_F030 对齐�?*/
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳�?*/
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_READINFO 8   /* 设备信息上报（readinfo 指令回复�?
                               * 帧格�? 8|设备名|设备数量|发射功率(dBm)|保留(当前填计�?|
                               * @note 类型 8 曾预留给告警（WARN），告警从未实现�?
                               *       若将来要做告警，必须换一个类型号 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* 设备信息 */
#define DEFAULT_DEVICE_NAME  "v3-0"
char deviceName[32] = DEFAULT_DEVICE_NAME;  /* 可变设备名，�?Flash 加载 */
#define BATTERY_VAL  99   /* 电量默认值（%）：ADC 取到值前的兜底，上电即被刷新 */

/* 配置字符串（持久化到 Flash，可通过指令修改�?*/
#define DEFAULT_CONFIG_STR  "1,0M,3t,6"
char configStr[64] = DEFAULT_CONFIG_STR;

/* 农场中心坐标�?lat,lng"，空=未设置，�?C 指令下发并持久化�?*/
char centerStr[64] = "";

/* �������� int32 ���棨1e-5 �ȣ���GPS �ϱ����ƫ��ʹ�á�
 * centerStr ���ַ�����ʽ��Flash/�ϱ��ã�������������ֵ��ʽ����ƫ���ã���
 * ������ "���� Flash" �� "C ָ����������" ����ͬ������ */
static int32_t centerLat1e5 = 0;
static int32_t centerLng1e5 = 0;
static uint8_t centerValid  = 0;

/* Flash 存储地址（STM32F103C8T6 64KB，每项独占一页，擦除互不干扰�?*/
#define DEVNAME_FLASH_ADDR  0x0800F800  /* 设备名存储（倒数第二页） */
#define DEVNAME_MAGIC       0xA5A55A5B  /* 修改魔术字使�?Flash 数据失效 */
#define CONFIG_FLASH_ADDR   0x0800F400  /* 配置字符串存储（倒数第三页） */
#define CONFIG_MAGIC        0x5A5AA5A6  /* 修改魔术字使�?Flash 数据失效 */
#define TXPWR_FLASH_ADDR    0x0800F000  /* 发射功率存储（倒数第四页） */
#define TXPWR_MAGIC         0xA5A55A5C  /* 功率魔术�?*/
#define CENTER_FLASH_ADDR   0x0800EC00  /* 农场中心坐标存储（倒数第五页） */
#define CENTER_MAGIC        0xA5A55A5D  /* 中心坐标魔术�?*/
#define DEVCNT_FLASH_ADDR   0x0800E800  /* 子设备总台数存储（倒数第六页） */
#define DEVCNT_MAGIC        0xA5A55A5E  /* 设备台数魔术�?*/
#define SRCHTM_FLASH_ADDR   0x0800E400  /* GPS search time storage (7th page from end) */
#define SRCHTM_MAGIC        0xA5A55A5F  /* change to invalidate flash data */
#define VER_FLASH_ADDR      0x0800E000  /* firmware version marker (8th page from end) */
#define VER_MAGIC           0xA5A55A60  /* change to invalidate version marker */
#define APP_VERSION         11u          /* bump on EVERY new program: if it differs
                                       * from the Flash marker, the first cycle
                                       * reports all readinfo items once, then
                                       * the new version is stored */
#define GPS_SEARCH_MIN_MIN  1           /* searchtime cmd: min (minutes) */
#define GPS_SEARCH_MAX_MIN  10          /* searchtime cmd: max (minutes) */
#define GPS_SEARCH_DEFAULT_MIN 4       /* default search max (minutes) */

/* 上报间隔：由 configStr �?1 段决定（分钟），�?"30,0M,3t,6" = 30 分钟
 * configStr 四段含义:
 *   �?1 �?"1"  = 工作期间的上报周�?分钟)，范�?1~60
 *   �?2 �?"0M" = 工作时间段起始，base62 双字符编�?�?idx 22
 *   �?3 �?"3t" = GPS 自动上报时间段，base62 双字符编�?�?idx 241
 *   �?4 �?"6"  = 保留 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;    /* LoRa PAN3029 */
UART_HandleTypeDef huart1;  /* 调试打印 */
UART_HandleTypeDef huart3;  /* GPS 模块 USART3 (PB11 RX, 9600, 仅接�? */

/* USER CODE BEGIN PV */

/* 时间同步变量（时间基�?= RTC 秒计数器，STOP 深度休眠期间继续走） */
static uint32_t syncedTimestamp = 0;  /* 同步到的 Unix 时间�?*/
static uint32_t syncedRtcSec = 0;     /* 对时那一刻的 RTC 计数（秒�?*/
static uint8_t  timeSynced = 0;       /* 是否已同步时�?*/
static uint32_t anchorRtcSec = 0;
/* ���м�ʱ��Դʶ�𣺶�ʱ֡�� 5 �� Relay ID��CENTEN �� RELAY_ID ���·�����
 * ��̨�м�ʱ�ӻ�׼�в� �� �����м̾�����ê�����²���ƫ�
 * ��ֹ����һ̨����ȥ���ê��ƫ�����Ⱦ���񲹳�ֵ */
static char    lastSyncRelay[8] = "";
static uint8_t lastSyncRelaySeen = 0;     /* 首次对时�?RTC 基准（用于晶振偏差计算） */
static uint16_t gpsContTotalMin = 0;   /* upgps 指令带的分钟数参数：
                                        *   0   = 单次搜星上报（现有逻辑�?
                                        * 1~60  = 连续定位模式：这么长时间内不休眠�?
                                        *         �?GPS_CONT_INTERVAL_SEC 秒上报一次坐�?
                                        * 用完即清零，故失败重试时只会走单次逻辑 */
static uint32_t anchorTimestamp = 0;  /* 首次对时�?Unix 时间戳基�?*/
/* --- 晶振偏差补偿（收敛后才启用）---
 * 每次对时测出偏差(�?小时, �?本地�?；连续两次测量相�?�?DRIFT_CONVERGE_TOL
 * 才视为收敛并启用补偿：休眠闹钟按偏差换算（准点），本机时间也回到真实轴�?
 * 未收敛时两值均�?0 �?所有换算退化成原行为，零风�?*/
static int32_t  rtcDriftPerHour = 0;  /* 当前采用的晶振偏差（�?小时�?*/
static uint8_t  driftConverged = 0;   /* 1=偏差已收�? 补偿生效 */
static int32_t  lastDriftSample = 0;  /* 上一次对时测得的偏差（收敛判定用�?*/
static uint8_t  configNeedReport = 1;  /* 配置需要上报（开�?1，收到指令更�?1，上报后=0�?*/
static uint8_t  centerNeedReport = 0;  /* 中心坐标需要上报（收到 C 指令=1，上报后=0�?*/
static uint8_t  readInfoNeedReport = 0;/* 设备信息需要上报（收到 readinfo 指令=1，上报后=0�?*/
/* readinfo 指令�?value 参数（每次只上报一�?8 帧，帧长 �?2 字节）：
 *   0=设备概况(默认,无value/乱码)  1=晶振偏差  2=最近收�?RSSI/SNR */
static uint8_t  readInfoValue = 0;
/* readinfo value: 1=drift 2=last rssi/snr 3=farm center 4=device overview
 * 5=chip UID (tail word, 8 hex) 6=device settings (existing config frame);
 * 0 (default/missing/garbled) = report all (1..6) back to back */
static uint8_t  lastRxRssi = 0;        /* 最近一次接收窗口收到的 RSSI 原�?上报�?-256) */
static uint8_t  lastRxSnr  = 0;        /* 最近一次接收窗口收到的 SNR */
static uint8_t  lastRxSeen = 0;        /* 1=开机至今收到过�?未收到过上报 noRX) */
static uint8_t  gpsWaitReport = 0;     /* 收到 upgps 指令=1，处理完自动清零 */
/* gpsNeedRetry removed: a failed upgps search reports once and is DONE - no automatic re-search at later slots (each retry cycle burned ~4-8min of GPS power when no satellites were visible). */
/* Link health uses monotonic RTC time, independent of wall-clock sync. */
static uint32_t linkLastRtc, linkSilentSec;
static uint8_t linkPollReady, linkWasEnabled, linkRecoveryStep;
static uint8_t clockFallback;

/* 提前搜星结果（需�?GPS 的上报轮，唤醒后先搜星再等到点上报，保证时间点不后移�?*/
static uint8_t  gpsPreSearched = 0;    /* 1=本轮已提前搜过星，OnMaster 直接取结果上�?*/
static uint32_t gpsSearchMaxMs = GPS_SEARCH_DEFAULT_MIN * 60000UL; /* GPS search max ms; searchtime cmd 1~10 min, flash-saved */
static uint8_t  gpsPreFixOk    = 0;    /* 预搜结果�?=拿到有效坐标 */
static uint32_t gpsPreSec      = 0;    /* 预搜耗时（秒），上报帧第 4 段用 */
static uint32_t gpsPreBytes    = 0;    /* 预搜期间 GPS 串口字节数，无定位时上报帧第 4 段用 */
#define TX_COUNT_MAX   999             /* 上报帧计数上限：超过后回绕到 1
                                        * 好处是帧里这一字段恒为 1~3 位数字，
                                        * 帧长稳定，也不用担心 uint32 溢出(�?49 天后) */
#define TX_COUNT_ENABLE 1             /* 帧尾计数段开关：1=帧尾�?|计数|�?=不带（省字节�?
                                        * @note 0 �?tx_count 仍在内部递增（窗口静默标记靠它）�?
                                        *       只是不再拼进帧里；云端解析帧时注意段数变�?*/

static uint32_t tx_count = 0;          /* 上报帧计数（每发一�?LoRa +1，见 txCountNext()�?
                                        * 放在文件作用域：配置�?中心坐标�?定位�?
                                        * 也都要带这个计数，便于云端判重和算丢�?*/
#define WINDOW_PRINT_MAX_ROUNDS 20     /* 上报超过此次数后停止所有接收窗口打�?*/
static uint8_t windowPrintOff = 0;     /* 一次性标记：�?1 后窗口静默（回绕也不恢复�?
                                        * 断电重启才复位）——长期部署减少串口日志噪�?*/
/* Only scheduled type 2/type 1 reports set this monotonic RTC anchor.
 * Command UPGPS (type 5) and auxiliary replies must not affect it. */
static uint32_t periodicLastRtc;
static uint8_t periodicSeen;
static uint32_t nextTxTimestamp = 0;   /* 下次上报的绝对时间戳（秒�?*/
/* 开机前 3 轮快速上报配额：配置周期 > 1 分钟时，�?3 轮先�?1 分钟
 * （让设备尽快对时、收配置、进入正常时隙节奏）�? 轮后恢复配置周期�?
 * 每完成一�?OnMaster 递减一次；掉电重启重新�?3 �?*/
static uint32_t bootTxRounds = 0;   /* uplink frames since boot, independent of wire counter */
static uint8_t  fastStartRounds = 3;
static uint8_t  currentTxPower = DEFAULT_PWR;  /* 当前发射功率（从 Flash 加载或指令修改） */
static uint8_t  deviceCount    = 30;  /* 分时隙上报的子设备总台数（默认 30，可�?
                                       * devicecount 指令下发并持久化�?Flash�?*/
static uint8_t  loraInitialized = 0;  /* LoRa 是否已初始化（首�?TX 前才初始化） */
static uint8_t  cachedBatteryPercent = BATTERY_VAL;  /* 缓存电量百分比（0~99�?
                                        * 每次 LoRa 唤醒时读一�?ADC 刷新�?
                                        * 上报帧里带的就是这个�?*/

/* --- 电池电量检测（PA0 / ADC1_CH0�?--
 * @note  硬件：电�?�?100K �?PA0 �?100K �?GND，即 1:1 分压
 *        �?V_bat = ADC电压 × 2，电池满�?4.2V 时分压点�?2.1V（未�?3.3V 量程�?
 *        用寄存器方式操作 ADC1，不依赖 stm32f1xx_hal_adc.c
 */

/* --- GPS 模块（USART3, PB11 RX, 9600�?--
 * @note  电源�?PB15/PB1 双脚控制（gpsPowerOn/Off），中断里只�?
 *        "塞入环形缓冲"这个最轻的动作，解析和打印全放主循�?
 */
#define GPS_RING_LEN   512   /* 环形缓冲：主循环�?LoRa 收发阻塞时先攒着 */
#define GPS_LINE_LEN   128   /* 单条 NMEA 语句上限 */
#define GPS_FIX_TTL_MS 60000 /* 定位有效�?ms)：超过这么久没刷新就算失�?*/
#define GPS_SEARCH_DEFAULT_MIN_MS (GPS_SEARCH_DEFAULT_MIN * 60000UL) /* was GPS_WAIT_TIMEOUT_MS, now runtime var gpsSearchMaxMs */
                                    /* @note 冷启动首次定位最坏要 12.5 分钟�?
                                    *       天线条件差时 3 分钟不够，先放宽�?4 分钟实测 */
#define GPS_WAIT_MIN_MS     10000  /* upgps 最短搜星时�?= 10 �?
                                    * @note 收到指令后不能马上发 LoRa：中继此时还没准备好接收�?
                                    *       发太快大概率丢帧。即使立刻就有坐标，也要至少等够�?10 �?*/
/* 连续定位模式：每一�?*最�?*占用的时间（搜星 + 等待�?
 * 跟踪上报间隔 = 设置的搜星最大时�?GPS_WAIT_TIMEOUT_MS)，但下限 3 分钟�?
 *   - 搜星上限调大/调小时，跟踪间隔自动跟随（间隔与单轮最坏耗时一致，
 *     搜星慢的轮本轮时长自然以搜星耗时为准，不会叠加催下一轮）�?
 *   - 再设 180 秒下限，防止把搜星上限改小后跟踪上报过密�?
 * 跟踪期间 GPS 一直保持供电、不进休眠；类型 5 上报不开接收窗口 */
#define GPS_CONT_INTERVAL_SEC \
    (((gpsSearchMaxMs) / 1000u) > 180u ? ((gpsSearchMaxMs) / 1000u) : 180u)
#define GPS_CONT_MAX_MIN      60   /* upgps 分钟数参数上�?= 60 分钟 */
#define GPS_CONT_MAX_SEC      ((uint32_t)GPS_CONT_MAX_MIN * 60u)   /* 60 分钟的秒数：
   * 跟踪目标就是定位成功 —�?一直拿不到有效坐标时，跟踪期限自动保底延长
   * 到这里（期间每轮照常上报 0.0,0.0），拿到坐标后恢复参数设定的时长 */
/* 需要搜星的上报轮：提前唤醒�?= 搜星上限 + 5 秒（LoRa 初始化倒计时）
 * 醒来先搜星，搜到就把 GPS 断电再睡到上报点�?5 秒，到点准点发帧�?
 * 搜不到也到点�?"0.0,0.0|串口字节数|"，保证上报时间点不后�?*/
#define GPS_PRESEARCH_LEAD_SEC ((gpsSearchMaxMs / 1000u) + 5u)
#define GPS_PWR_ON_DELAY_MS  1000  /* PB15 上电后等待模块输�?NMEA 的时�?ms)
                                    * @note 电源冷启动（不用 WAKE_UP 休眠模式），
                                    *       比Standby热唤醒慢，给�?1 �?*/

/* GPS 电源控制总开�?(PB15): 1=�?MCU 控制电源通断, 0=不动电平（核对阶段）
 * �?1 时的行为（当前生效，极�? �?上电, �?断电�?
 *   - 上电 PB15 默认�?�?GPS 断电（完全关机，0 电流�?
 *   - 搜星�?gpsPowerOn() 置低 �?上电开机，搜完 gpsPowerOff() 置高 �?断电
 *   - 深度休眠等待期模块始终断电，不耗任何电�?
  *   GPS power control is unconditional (formal-board flow).
 */

/* 链路沉默看门狗：工作时间内连续这么久没收到任�?LoRa 信息 �?真复位自恢复
 * �? 小时。跨 STOP 深度休眠连续计时——用 RTC 时间戳，休眠机制完全不变�?
 *   连续定位跟踪期间不检查，避免打断主动任务�?*/
#define LINK_SILENCE_REBOOT_SEC  (2u * 3600u)

/* 晶振偏差收敛阈值（�?小时）：连续两次对时测得的偏差相�?�?此值视为收敛�?
 * 30 分钟对时区间�?1 秒的测量误差 �?12 �?小时，取 15 留裕�?*/
#define DRIFT_CONVERGE_TOL      30
/* @note  ԭ 15 s/h ���ϣ�30 ���Ӷ�ʱ���� 1 �����������12 s/h�����β���
 *        ��Ҫ"ǡ�ýӽ�"��������LSI ����(ƫ��������¶�Ư)���ܳ��ڲ�������
 *        30 s/h �� 30 �����ֵĲ����в�� ��15 �룬��ÿ�ζ�ʱ�����ز��ƫ */
/* 偏差样本剔除上限（秒/小时）：LSI 晶振最差约 ±5%（�?80�?h），
 * 超过 ±300 视为中继自身授时错误（中继时间跳变导致），样本直接丢弃—�?
 * 不参与收敛、不污染补偿值，收敛结果只由可信样本决定 */
#define DRIFT_SAMPLE_MAX        300



typedef struct {
  int32_t  lat;     /* 纬度 ×1e5（北纬为正），如 26.52958 �?2652958 */
  int32_t  lng;     /* 经度 ×1e5（东经为正） */
  uint8_t  sats;    /* 参与定位的卫星数 */
  /* 以下�?GPS �?UTC 日期时间（来�?RMC），未收到过则全�?0
   * 用于搜星状态行�?年份是否可信"判断与时间显�?*/
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  uint8_t  hour;
  uint8_t  minute;
  uint8_t  second;
} GpsFix_t;

static const uint8_t gpsDaysInMonth[12] =   /* 平年每月天数（闰�?2 月单独修正） */
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static GpsFix_t     gpsFix;
static uint8_t      gpsFixValid = 0;   /* 最近一�?GGA 是否有效定位 */
static uint8_t      gpsPowerState = 0; /* 当前 PB15 状态：1=上电搜星, 0=断电
                                        * （断电诊断用：断电时不该有串口数据） */
static uint32_t     gpsRxCount = 0;    /* 收到的字节数（诊断用�?*/
static uint32_t     gpsFixTick = 0;    /* 最近一次拿到有效定位的时刻（HAL_GetTick�?*/
static uint8_t      gpsPrintEn = 0;    /* GPS 打印总开关（1=打印, 0=静默�?
                                        * 默认 0：正常启�?运行不打任何 GPS 日志�?
                                        * 收到 upgps 后临时置 1，处理完自动恢复原值；
                                        * @note  只影响打印，不影响接�?解析/上报 */
static volatile uint8_t  gpsRing[GPS_RING_LEN];
static volatile uint16_t gpsWr = 0;    /* 写指针（ISR 更新�?*/
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
static uint32_t getTodaySecond(void);
static uint32_t getCurrentTimestamp(void);
static uint8_t isLeapYear(uint16_t year);
static void formatTimestamp(uint32_t ts, char *buf, uint8_t len);
static void reportPendingMessages(void);   /* 回发待上报的配置/中心坐标�?*/
static void OnMaster(void);
static void parse_rx_message(uint8_t *buf, uint8_t len);
static uint8_t cmdIsForMe(char *fields[]);   /* 指令目标校验(统一跳过打印) */
static void loadDeviceNameFromFlash(void);
static void saveDeviceNameToFlash(const char *name);
static void loadConfigFromFlash(void);
static void saveConfigToFlash(const char *str);
static void loadTxPowerFromFlash(void);
static void saveTxPowerToFlash(uint8_t power);
static void loadDeviceCountFromFlash(void);  /* 子设备总台�?默认30, devicecount 指令可改) */
static void saveDeviceCountToFlash(uint8_t count);
static void loadCenterFromFlash(void);
static void saveCenterToFlash(const char *str);
static uint8_t centerCoordValid(int32_t lat, int32_t lng);
static void    deg1e5ToStr(int32_t v, char *buf);
static uint8_t parseDeg1e5(const char *s, int32_t *out);
static void loraInitFirstTime(void);
static uint8_t loraTryInit(void);
static void loraSleep(void);
static void sendConfigMessage(void);
static void sendResetInfoMessage(void);   /* boot reset cause frame (type 8) */

/* boot reset cause ("POR"/"SW"/"IWDG"), reported once over LoRa after boot:
 * field deployments have no serial port, so the cloud must see reboots */
static char    bootRstCause[8] = "POR";
static uint8_t bootRstPending  = 0;       /* 1 = send the reset report frame */
static void sendCenterMessage(void);
static void sendReadInfoMessage(void);  /* 设备信息上报 8|设备|设备数量|发射功率|保留| */
static void sendGpsPositionMessage(uint8_t hasFix, uint8_t useGpsType,
                                   uint32_t searchSec, uint32_t rxBytes);
static uint8_t gpsSearchAndReport(uint8_t useGpsType);
static uint8_t gpsRunContinuous(uint16_t totalMin, uint8_t useGpsType);
static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs);
static void    gpsPrintSearchStatus(uint32_t elapsedMs);   /* 搜星状态行(每秒一�? */
static uint32_t getConfigIntervalSec(void);
static void    getConfigFieldStr(uint8_t idx, char *out, uint8_t outLen);
static int16_t twoCharToIndex(const char *s);    /* base62 双字�?�?索引(0~275) */
static uint8_t timeIdxToRange(int16_t idx, uint8_t *startHour, uint8_t *endHour);
static int16_t getConfigWorkStartIdx(void);      /* 工作时间�?�?�?"0M") */
static int16_t getConfigGpsEndIdx(void);         /* GPS 上报时间�?�?�?"3t") */
static uint8_t  getDeviceIndex(void);
static uint32_t getSlotOffsetSec(uint32_t intervalSec);
static void markPeriodicReport(void);
static void calcNextTxTime(void);
static void MX_USART3_UART_Init(void);
static void gpsTask(void);
static void gpsTrySyncFromSat(void);        /* 卫星授时兜底（本机未对时时用一次） */
static void parseNmeaLine(char *line);
static uint8_t nmeaField(const char *line, uint8_t n, char *dst, uint8_t dstLen);
static int32_t nmeaToDeg(const char *v, char dir);
static uint8_t gpsFixFresh(void);
static void linkSilenceCheck(void);
static void linkNoteValid(void);
static uint32_t rtc_read_counter(void);
static uint8_t configTimeFieldValid(const char *f);
static void getConfigFieldStrFrom(const char *src, uint8_t idx, char *out, uint8_t outLen);
static void nvSaveOrReset(uint32_t page, uint32_t magic, const void *data, uint32_t length);
static void wdg_init(void);               /* HW watchdog: ~10s, fed in chunked sleep */
static void wdg_feed(void);       /* 链路沉默看门�?2小时无LoRa→真复位) */
static void    printTimeWindows(const char *tag);   /* 打印当前生效的两个时间段 */
static uint8_t isInGpsWorkWindow(void);   /* 当前是否�?GPS 工作时间段内(config �?�? */
static uint8_t isInGpsWorkWindowAt(uint32_t ts);  /* 指定时刻是否�?GPS 时间段内 */
static uint8_t gpsBatteryOk(void);
static uint8_t gpsNeededTimelyAt(uint32_t ts);    /* 该时刻是�?必须准点"�?GPS �?类型1) */
static void    gpsPreSearch(void);                /* 提前搜星（到点前必须完成�?*/
static void    openRxWindow3000(void);    /* 开 3 秒接收窗口并解析（可重复调用�?*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
static void gpsPowerOn(void);               /* GPS 唤醒：PB1(WAKE_UP) 置高 �?Full on */
static void gpsPowerOff(void);              /* GPS 休眠：PB1(WAKE_UP) 置低 �?Standby */
static uint16_t ADC1_Read(void);            /* ADC1 单次读取�?2 位） */
static uint8_t  GetBatteryPercent(void);    /* ADC �?�?电池电压 �?电量百分�?*/
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
  * @brief  printf 底层输出函数重定向（USART1�?
  */
PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 20u);
  return ch;
}

/**
  * @brief  �?Flash 加载设备�?
  * @note   如果 Flash 中有有效魔术字则加载，否则使用默认名
  */
static void loadDeviceNameFromFlash(void)
{
  char value[sizeof(deviceName)];
  if (NvStore_Load(DEVNAME_FLASH_ADDR, DEVNAME_MAGIC, value, sizeof(value), 1u) &&
      value[0] && strlen(value) <= 5u)
    strcpy(deviceName, value);
}

/**
  * @brief  保存设备名到 Flash
  * @param  name: 要保存的设备名字符串
  */
static void saveDeviceNameToFlash(const char *name)
{
  nvSaveOrReset(DEVNAME_FLASH_ADDR, DEVNAME_MAGIC, name, strlen(name) + 1u);
}

/**
  * @brief  �?Flash 加载配置字符�?
  */
static void loadConfigFromFlash(void)
{
  char value[sizeof(configStr)], wf[8], gf[8];
  uint32_t minutes;
  if (!NvStore_Load(CONFIG_FLASH_ADDR, CONFIG_MAGIC, value, sizeof(value), 1u)) return;
  minutes = (uint32_t)atoi(value);
  getConfigFieldStrFrom(value, 1u, wf, sizeof(wf));
  getConfigFieldStrFrom(value, 2u, gf, sizeof(gf));
  if (minutes >= 1u && minutes <= 60u && configTimeFieldValid(wf) &&
      configTimeFieldValid(gf)) strcpy(configStr, value);
}

/**
  * @brief  保存配置字符串到 Flash
  */
static void saveConfigToFlash(const char *str)
{
  nvSaveOrReset(CONFIG_FLASH_ADDR, CONFIG_MAGIC, str, strlen(str) + 1u);
}

/**
  * @brief  �?Flash 加载子设备总台数（devicecount 指令持久化的值）
  * @note   无保�?/ 值超范围(1~60)时用默认 30
  */
static void loadDeviceCountFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(DEVCNT_FLASH_ADDR, DEVCNT_MAGIC, &value, sizeof(value), 0u) &&
      value >= 1u && value <= 60u) deviceCount = (uint8_t)value;
}

/**
  * @brief  保存子设备总台数到 Flash
  */
static void saveDeviceCountToFlash(uint8_t count)
{
  uint32_t value = count;
  nvSaveOrReset(DEVCNT_FLASH_ADDR, DEVCNT_MAGIC, &value, sizeof(value));
}
/**
  * @brief  Load tx power from flash (restore after damage, power funcs)
  */
static void loadTxPowerFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(TXPWR_FLASH_ADDR, TXPWR_MAGIC, &value, sizeof(value), 0u) &&
      value >= 10u && value <= 20u) currentTxPower = (uint8_t)value;
}

/**
  * @brief  Save tx power to flash
  */
static void saveTxPowerToFlash(uint8_t power)
{
  uint32_t value = power;
  nvSaveOrReset(TXPWR_FLASH_ADDR, TXPWR_MAGIC, &value, sizeof(value));
}


/**
  * @brief  Load GPS search max time from flash (searchtime cmd)
  */
static void loadSearchTimeFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(SRCHTM_FLASH_ADDR, SRCHTM_MAGIC, &value, sizeof(value), 0u) &&
      value >= GPS_SEARCH_MIN_MIN * 60000u && value <= GPS_SEARCH_MAX_MIN * 60000u)
    gpsSearchMaxMs = value;
}

/**
  * @brief  Save GPS search max time to flash
  */
static void saveSearchTimeToFlash(uint32_t ms)
{
  nvSaveOrReset(SRCHTM_FLASH_ADDR, SRCHTM_MAGIC, &ms, sizeof(ms));
}
/**
 * @brief  �?Flash 读出一个以 '\0' 结尾的字符串
 */
static void nvSaveOrReset(uint32_t page, uint32_t magic,
                          const void *data, uint32_t length)
{
  if (!NvStore_Save(page, magic, data, length)) Safety_Reset(RESET_FLASH);

}

/**
 * @brief firmware upgrade marker: compare APP_VERSION against the Flash copy
 * @note  new program flashed (or marker page erased/invalid) -> flag ONE
 *        readinfo cycle (readInfoValue=0 reports all 6 kinds back-to-back,
 *        2s apart) and store the new version. Same version -> nothing extra.
 */
static void checkVersionReportFlag(void)
{
  uint32_t stored = 0;
  uint32_t cur    = APP_VERSION;

  if (NvStore_Load(VER_FLASH_ADDR, VER_MAGIC, &stored, sizeof(stored), 0u) &&
      stored == cur)
    return;

  readInfoNeedReport = 1;
  readInfoValue = 0;   /* 0 = report all 6 readinfo kinds once */
  printf("[VER] fw %lu new (flash %lu), readinfo x6 pending\r\n",
         (unsigned long)cur, (unsigned long)stored);
  nvSaveOrReset(VER_FLASH_ADDR, VER_MAGIC, &cur, sizeof(cur));
}

/**
 * @brief  把一个字符串（含结尾 '\0'）按半字写入 Flash
 * @note   STM32F1 只能半字编程；用 i+1<=len 兜底�?
 *         避免读到源串 '\0' 之后的越界字�?
 */
/* String writes are handled by the journal, including the final NUL. */

/**
 * @brief  �?Flash 加载农场中心坐标
 * @note   如果 Flash 中有有效魔术字则加载，否则保持为空（未设置）
 */
static void loadCenterFromFlash(void)
{
  char value[sizeof(centerStr)];
  int32_t la, lo;
  const char *comma;
  if (!NvStore_Load(CENTER_FLASH_ADDR, CENTER_MAGIC, value, sizeof(value), 1u)) return;
  comma = strchr(value, ',');
  if (comma && parseDeg1e5(value, &la) && parseDeg1e5(comma + 1, &lo) &&
      centerCoordValid(la, lo)) {
    strcpy(centerStr, value);
    centerLat1e5 = la;
    centerLng1e5 = lo;
    centerValid = 1u;
  }
}

/**
 * @brief  保存农场中心坐标�?Flash（独立页，与其它配置项互不影响）
 * @param  str: 归一化后的坐标字符串，格�?"lat,lng"
 */
static void saveCenterToFlash(const char *str)
{
  nvSaveOrReset(CENTER_FLASH_ADDR, CENTER_MAGIC, str, strlen(str) + 1u);
}

/**
 * @brief  校验经纬度是否在合法地理范围�?
 * @retval 1=合法, 0=非法
 */
static uint8_t centerCoordValid(int32_t lat, int32_t lng)
{
  if (lat < -9000000 || lat > 9000000)     /* ±90 �?×1e5 */
    return 0;
  if (lng < -18000000 || lng > 18000000)   /* ±180 �?×1e5 */
    return 0;
  return 1;
}

/**
  * @brief  首次初始�?LoRa 模块（从关机状态启动）
  * @note   仅在第一�?TX 前或�?deepsleep 唤醒后调�?
  */
static uint8_t loraTryInit(void)
{
  uint8_t tries;
  HAL_NVIC_DisableIRQ(EXTI2_IRQn);
  Safety_Begin(STAGE_RADIO, 10000u);
  for (tries = 0u; tries < 3u; ++tries) {
    wdg_feed();
    HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20u);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100u);
    if (rf_init() == RF_OK && rf_set_default_para() == RF_OK &&
        rf_set_tx_power(currentTxPower) == RF_OK) break;
    HAL_Delay(200u);
  }
  loraInitialized = (tries < 3u);
  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_2);
  HAL_NVIC_ClearPendingIRQ(EXTI2_IRQn);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
  Safety_Begin(STAGE_MAIN, 15000u);
  return loraInitialized;
}

static void loraInitFirstTime(void)
{
  if (!loraTryInit()) Safety_Reset(RESET_RADIO);
  printf("[LoRa] %dM SF%d PWR=%d\r\n",
         DEFAULT_FREQ / 1000000, DEFAULT_SF, currentTxPower);
}

/**
 * @brief  上报计数 +1（每发一�?LoRa 都调一次）�? ~ TX_COUNT_MAX 回绕
 * @note   必须在组�?snprintf)之前调用，帧里带的是递增后的�?
 */
static void txCountNext(void)
{
  tx_count++;
  if (tx_count > TX_COUNT_MAX)
    tx_count = 1;
  bootTxRounds++;   /* independent of tx_count wraparound */
  /* 上报次数超过阈值：一次性置位窗口静默标�?*/
  if (tx_count > WINDOW_PRINT_MAX_ROUNDS)
    windowPrintOff = 1;
}

/**
 * @brief  LoRa 单发一帧（阻塞等发送完成），所有上行帧共用
 * @param  msg: �?'\0' 结尾的帧字符串（连同结束符一起发送）
 * @note   原本 5 处上行帧各自复制同一段三行发送样板，统一收敛于此
 */
static void loraTx(const char *msg)
{
  uint32_t tx_time = 1000u, start = HAL_GetTick();
  Safety_Begin(STAGE_TX, 6000u);
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  if (rf_single_tx_data((uint8_t *)msg, strlen(msg) + 1u, &tx_time) != RF_OK)
    Safety_Reset(RESET_TX);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE) {
    if ((uint32_t)(HAL_GetTick() - start) >= 5000u) Safety_Reset(RESET_TX);
    wdg_feed();
  }
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  Safety_Begin(STAGE_MAIN, 15000u);
  printf("[TX] %s\r\n", msg);
}

/**
  * @brief  发送配置消息（重启后只发一次）
  * @note   格式: MSG_TYPE_CONFIG|deviceName|config_str|计数|
  */
  static void sendConfigMessage(void)
  {
   char tx_msg[TX_LEN];

   txCountNext();
#if TX_COUNT_ENABLE
   snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
            MSG_TYPE_CONFIG, deviceName, configStr, (unsigned long)tx_count);
#else
   snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|",
            MSG_TYPE_CONFIG, deviceName, configStr);
#endif

  loraTx(tx_msg);
}

/**
 * @brief  发送设备信息消息（readinfo 指令回复，每次只发一条，帧长 �?2 字节�?
 * @note   �?readInfoValue 组帧�?
 *           0(缺省/乱码): 8|设备|台数T|功率dbm|版本|计数|   (26~29 字节)
 *           1:            8|设备|偏差s/h|计数|             (19 字节, 偏差范围±300)
 *           2:            8|设备|Rssi,Snr|计数|            (20 字节; 开机未收过包报 noRX)
 * @note   概况帧去掉了�?433HZ �?—�?最坏情�?计数999)原帧 33 字节�?32 上限
 */
static void sendReadInfoMessage(void)
{
  char tx_msg[TX_LEN];

  /* value 0 (default/missing/garbled): report all 6 kinds back to back,
   * 2s apart so CENTEN can relay each frame to DTU; then normal sleep flow */
  if (readInfoValue == 0) {
    uint8_t v;

    /* 6 frames x (tx+2s gap) ~ 15s: re-arm the MAIN stage deadline,
     * it would otherwise fire at 15s mid-burst (Safety_Feed cannot
     * extend it - only Safety_Begin re-arms phaseTick/phaseLimit) */
    Safety_Begin(STAGE_MAIN, 30000u);
    for (v = 1; v <= 6; v++) {
      readInfoValue = v;
      wdg_feed();   /* 6 frames x (tx~1s + 2s gap) = ~15s total, must feed */
      sendReadInfoMessage();
      if (v < 6)
        HAL_Delay(2000);
    }
    readInfoValue = 0;
    return;
  }

  /* value 6: device settings = reuse the existing config frame
   * (6|dev|configStr|cnt|, type MSG_TYPE_CONFIG, cloud already parses it) */
  if (readInfoValue == 6) {
    sendConfigMessage();
    return;
  }

  txCountNext();
  if (readInfoValue == 1) {
#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ds/h|%lu|",
             MSG_TYPE_READINFO, deviceName, (int)rtcDriftPerHour,
             (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ds/h|",
             MSG_TYPE_READINFO, deviceName, (int)rtcDriftPerHour);
#endif
  } else if (readInfoValue == 2) {
    if (lastRxSeen) {
#if TX_COUNT_ENABLE
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ddBm,%d|%lu|",
               MSG_TYPE_READINFO, deviceName,
               (int)lastRxRssi - 256, (int)lastRxSnr,
               (unsigned long)tx_count);
#else
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ddBm,%d|",
               MSG_TYPE_READINFO, deviceName,
               (int)lastRxRssi - 256, (int)lastRxSnr);
#endif
    } else {
#if TX_COUNT_ENABLE
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|noRX|%lu|",
               MSG_TYPE_READINFO, deviceName, (unsigned long)tx_count);
#else
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|noRX|",
               MSG_TYPE_READINFO, deviceName);
#endif
    }
  } else if (readInfoValue == 3) {
    /* 农场中心坐标：未设置过回 "0,0"（与 C 指令回包逻辑一致） */
#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
             MSG_TYPE_READINFO, deviceName,
             (centerStr[0] != '\0') ? centerStr : "0,0",
             (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|",
             MSG_TYPE_READINFO, deviceName,
             (centerStr[0] != '\0') ? centerStr : "0,0");
#endif
  } else if (readInfoValue == 5) {
    /* chip UID: 96-bit unique ID @0x1FFFF7E8, report tail word as 8 hex.
     * full 24-hex would exceed the 32-byte frame cap (8|dev|24hex|cnt| = 40) */
    uint32_t uidW = *(volatile uint32_t *)0x1FFFF7F0u;

#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%08lX|%lu|",
             MSG_TYPE_READINFO, deviceName,
             (unsigned long)uidW, (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%08lX|",
             MSG_TYPE_READINFO, deviceName, (unsigned long)uidW);
#endif
  } else {
#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%uT|%ddbm|%s|%lu|",
             MSG_TYPE_READINFO, deviceName, (unsigned int)deviceCount,
             (int)currentTxPower, DEMO_VER, (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%uT|%ddbm|%s|",
             MSG_TYPE_READINFO, deviceName, (unsigned int)deviceCount,
             (int)currentTxPower, DEMO_VER);
#endif
  }

  loraTx(tx_msg);
}

/**
 * @brief  发送农场中心坐标消息（1|deviceName|lat,lng|计数|�?
 * @note   本项目无 DTU，回复上报走 LoRa 回到集中器，由集中器转发云端
 *         未设置过坐标时第 3 段发 "0,0"，保证帧结构固定�?
 *         云端/小程�?split('|') 不会拿到空串
 */
static void sendCenterMessage(void)
{
  char tx_msg[TX_LEN];
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  txCountNext();
#if TX_COUNT_ENABLE
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|%lu|",
           MSG_TYPE_GPS, deviceName, coord, (unsigned long)tx_count);
#else
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s|",
           MSG_TYPE_GPS, deviceName, coord);
#endif

  loraTx(tx_msg);
}

/**
 * @brief  串口字节�?�?紧凑可读格式（用于无定位时的上报帧第 4 段）
 * @param  bytes: 本轮搜星期间 GPS 串口收到的字节数
 * @param  buf:   输出缓冲（≥9 字节：最�?"999.99M\0" = 8�?
 * @retval 格式�?1024 �?"NByte"�?1M �?"X.XXK"；≥1M �?"X.XXM"
 * @note   两位小数用整数运算（×100/1024）实现，不引入浮点库�?
 *         长度上限：uint32 最�?4G �?"4095.99M" �?8 字符，buf 12 足够
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
 * @brief  发送当�?GPS 实时定位坐标�?
 * @param  hasFix: 1=有有效定位（发实测坐标）, 0=搜星失败（发 "0.0,0.0"�?
 * @param  useGpsType: 0=MSG_TYPE_UP_GPS(5), 1=MSG_TYPE_GPS(1)
 * @note   两种帧类型的用法（云端按帧类�?+ 上报时机区分�?
 *           MSG_TYPE_UP_GPS(5) = 收到 upgps 指令后的跟踪上报，上报后不开接收窗口
 *           MSG_TYPE_GPS(1)    = GPS 工作时间段内的自动上报、以及搜星失败后�?
 *                                重试上报，上报后要开 3 秒接收窗�?
 *         1 同时�?C 指令的农场中心坐标回显使用，但那是另一条指令路�?
 *         （收�?C 才发一次），与本函数的实时坐标不冲�?
 *         搜星失败时也必须发帧，第 3 段填 "0.0,0.0"�?
 *         保证云端 split('|') 拿到的段数固定，可直接判定为"无坐�?
 */
static void sendGpsPositionMessage(uint8_t hasFix, uint8_t useGpsType,
                                   uint32_t searchSec, uint32_t rxBytes)
{
  char tx_msg[TX_LEN];
  int      msgType = useGpsType ? MSG_TYPE_GPS : MSG_TYPE_UP_GPS;

  txCountNext();   /* 每发一�?LoRa 计数 +1（类�?1/5 都算一帧） */

  if (hasFix) {
    char latBuf[16], lngBuf[16];
    /* �������ƫ�ƣ�1e-5 �ȼ� 1e5 ��������Լ 1.1m �ֱ��ʣ���
     * ����������������ƫ���� 10km ��Χ�� -> �ϱ�����ƫ�ƣ���С���㣬
     * �м̾ݴ��ж�"�軹ԭ"���ӻ��������꣩�������ϱ��������ꡣ
     * ��Χ���Ʒ��Σ�lat ��0.09��(~10km)��lng ��0.105��(31��γ������ ~10km) */
    int32_t dlat = gpsFix.lat - centerLat1e5;
    int32_t dlng = gpsFix.lng - centerLng1e5;

    if (centerValid &&
        dlat <= 9000 && dlat >= -9000 && dlng <= 10500 && dlng >= -10500) {
#if TX_COUNT_ENABLE
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ld,%ld|%lu|%lu|",
               msgType, deviceName, (long)dlat, (long)dlng,
               (unsigned long)searchSec, (unsigned long)tx_count);
#else
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%ld,%ld|%lu|",
               msgType, deviceName, (long)dlat, (long)dlng,
               (unsigned long)searchSec);
#endif
    } else {
      /* С������ 5 λ������ ~1.1m��������֡������ */
      deg1e5ToStr(gpsFix.lat, latBuf);
      deg1e5ToStr(gpsFix.lng, lngBuf);
      /* �ж�λ: ����|�豸��|lat,lng|��������[|����|] */
#if TX_COUNT_ENABLE
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s,%s|%lu|%lu|",
               msgType, deviceName, latBuf, lngBuf,
               (unsigned long)searchSec, (unsigned long)tx_count);
#else
      snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%s,%s|%lu|",
               msgType, deviceName, latBuf, lngBuf,
               (unsigned long)searchSec);
#endif
    }
  } else {
    char bytesBuf[12];

    /* 无定�? 类型|设备名|0.0,0.0|串口字节数[|计数|]
     * @note  �?4 �?本轮搜星期间 GPS 模块吐到 MCU 的字节数�?
     *        诊断"模块没输�?(0 或极�?还是"有输出但没定�?(数值正�?�?
     *        格式化为紧凑可读形式: 1Byte / 0.32K / 34.22M（两位小数） */
    fmtRxBytes(rxBytes, bytesBuf, sizeof(bytesBuf));
#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|0.0,0.0|%s|%lu|",
             msgType, deviceName, bytesBuf, (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|0.0,0.0|%s|",
             msgType, deviceName, bytesBuf);
#endif
  }

  loraTx(tx_msg);
  if (useGpsType) markPeriodicReport();
}


/**
 * @brief  搜星 + 上报坐标（upgps 指令 �?失败自动重试 共用同一套流程）
 * @note   搜星期间临时打开 GPS 打印，结束后恢复成进入前的状�?
 * @retval 1=拿到有效坐标并已上报, 0=搜星失败已上�?"0.0,0.0"
 */
static uint8_t gpsSearchAndReport(uint8_t useGpsType)
{
  uint8_t  hasFix;
  uint8_t  savedPrint = gpsPrintEn;   /* 用局部变量保存，连续模式下每轮都能独立开�?*/
  uint32_t t0, searchSec;

  gpsPrintEn = 1;                    /* 搜星期间打开 GPS 打印 */
  gpsRxCount = 0;                    /* 本轮字节数从 0 起算（连续模式下每轮独立统计�?*/

  /* 搜星：最�?GPS_WAIT_MIN_MS(给中继留准备时间), 上限 GPS_WAIT_TIMEOUT_MS */
  t0 = HAL_GetTick();
  hasFix = gpsWaitForFix(GPS_WAIT_MIN_MS, gpsSearchMaxMs);
  searchSec = (HAL_GetTick() - t0 + 999u) / 1000u;   /* 搜星耗时，秒（向上取整） */
  HAL_Delay(1000);

  /* 无论是否定位成功都上报：失败时坐标段�?"0.0,0.0"，参数段填串口字节数 */
  sendGpsPositionMessage(hasFix, useGpsType, searchSec, gpsRxCount);

  /* 打印状态恢复成本轮进来看之前的样子（默认回到静默） */
  gpsPrintEn = savedPrint;
  printf("[GPS] done %s, gpsPrint=%s\r\n",
         hasFix ? "(已定�?" : "(未定�?", gpsPrintEn ? "ON" : "OFF");

  return hasFix;
}

/**
 * @brief  提前搜星（需�?GPS 的上报轮：唤醒后先搜，到点只发帧�?
 * @note   1) 搜星窗口 = 现在 �?(nextTxTimestamp - 5 �?，必须留 5 秒给 LoRa 发送；
 *         2) 窗口不足 GPS_WAIT_MIN_MS(10 �? 时直接跳过：标记"已完成、无坐标"�?
 *            到点�?"0.0,0.0|串口字节数|"，绝不让上报时间点后移；
 *         3) 窗口够但小于搜星上限时以窗口为准
 *            （例如上报周�?1 分钟 < 4 分钟搜星上限，只能在 1 分钟内尽力搜）；
 *         4) 搜完立即断电，结果暂�?gpsPreXxx，OnMaster 到点直接上报，不再现场搜�?
 */
static void gpsPreSearch(void)
{
  uint32_t nowTs    = getCurrentTimestamp();
  uint32_t deadline = (nextTxTimestamp > 5u) ? (nextTxTimestamp - 5u) : nowTs;
  uint32_t availMs  = (deadline > nowTs) ? ((deadline - nowTs) * 1000u) : 0u;
  uint32_t maxMs, minMs, t0;
  uint8_t  hasFix, savedPrint;

  if (!gpsBatteryOk()) return;

  gpsPreSearched = 1;      /* 无论搜不搜，都标记本轮预搜已处理 */
  gpsPreFixOk    = 0;
  gpsPreSec      = 0;
  gpsPreBytes    = 0;

  if (availMs < GPS_WAIT_MIN_MS) {
    printf("[GPS] prewindow short\r\n");
    return;
  }

  maxMs = (availMs > gpsSearchMaxMs) ? gpsSearchMaxMs : availMs;
  minMs = (maxMs > GPS_WAIT_MIN_MS) ? GPS_WAIT_MIN_MS : maxMs;

  printf("[GPS] presearch %lus(max %lus)\r\n",
         (unsigned long)(availMs / 1000u), (unsigned long)(maxMs / 1000u));

  savedPrint = gpsPrintEn;
  gpsPrintEn = 1;          /* 搜星期间打开 GPS 打印 */
  gpsRxCount = 0;

  gpsPowerOn();
  t0 = HAL_GetTick();
  hasFix = gpsWaitForFix(minMs, maxMs);
  gpsPreSec   = (HAL_GetTick() - t0 + 999u) / 1000u;
  gpsPreBytes = gpsRxCount;
  gpsPowerOff();           /* 搜完立即断电，等上报点时再发�?*/

  gpsPrintEn = savedPrint;
  gpsPreFixOk = hasFix;
  printf("[GPS] presearch done: %s, %lus, rx %luB\r\n",
         hasFix ? "(已定�? 到点直接上报缓存坐标)" : "(未定�? 到点上报 0.0,0.0)",
         (unsigned long)gpsPreSec, (unsigned long)gpsPreBytes);
}

/**
 * @brief  连续定位上报：totalMin 分钟内不休眠，每�?GPS_CONT_INTERVAL_SEC 秒上报一次坐�?
 * @note   每一轮的结构�?
 *           1) gpsSearchAndReport() —�?最�?10 秒、最�?GPS_WAIT_TIMEOUT_MS 搜星后发一帧坐�?
 *           2) 搜星提前结束时，把本轮补齐到 GPS_CONT_INTERVAL_SEC 秒再进下一�?
 *              （期间继续跑 gpsTask() 排空环形缓冲，但**不进入休�?*�?
 *         轮数 = 1 + floor((总时�?- 已用�? / GPS_CONT_INTERVAL_SEC)�?
 *           �?1 轮收到指令就立刻执行；之后每轮开�?*�?*先看剩余时间�?
 *           不足一轮的最短占用就提前结束，回到正常定时逻辑（该休眠就休眠）
 *         —�?这样"upgps 1 分钟 + 10 秒搜到星"只上�?1 次，不会在剩�?50 秒里连报 5 �?
 * @note   定位成功导向：跟踪的目标是拿到有效坐�?—�?
 *           - 只要还没拿到过坐标，期限自动保底延长�?GPS_CONT_MAX_SEC(60 分钟)�?
 *             期间每轮照常上报 0.0,0.0（让云端知道设备仍在跟踪中）�?
 *           - 一旦拿到坐标，期限立刻收回参数设定�?totalSec�?
 *             若已超时则本轮（刚报出坐标的这帧）就是最后一帧，正常收工�?
 *           - 参数�?60 的失败场景天然就�?60 分钟，无需再延�?
 * @warning MCU 与射频全程全速工作，分钟数直接线性影响耗电
 * @retval 1=期间至少有一轮拿到有效坐�? 0=全程都没有有效坐�?
 */
static uint8_t gpsRunContinuous(uint16_t totalMin, uint8_t useGpsType)
{
  if (!gpsBatteryOk()) return 0u;
  uint32_t totalSec    = (uint32_t)totalMin * 60u;
  uint32_t deadlineSec = totalSec;     /* 实际结束期限：无坐标时延长到 60 分钟 */
  uint32_t startTick   = HAL_GetTick();   /* 计时基准用开�?tick（毫秒）�?
   * 连续定位期间常会发生卫星授时/服务器对时，绝对时间戳会被整体平移，
   * 用时间戳差值会�?已用�?算成天文数字，导致第 1 轮后就被误判超时结束 */
  uint32_t rounds      = 0;
  uint8_t  everFixed   = 0;

  printf("[CONT] start %u min\r\n", (unsigned int)totalMin);

  while (1) {
    uint32_t slotStart = HAL_GetTick();
    uint32_t elapsed;
    uint32_t remain;
    uint8_t  hasFix;

    /* �?1 轮无条件执行：收�?upgps 指令就是要立刻搜星上�?*/
    rounds++;
    hasFix = gpsSearchAndReport(useGpsType);
    if (hasFix)
      everFixed = 1;

    if (hasFix) {
      printf("[CONT] #%lu fix\r\n", (unsigned long)rounds);

      /* 拿到坐标：期限收回参数设定值。若已超时，下面的判断会正常结束�?
       * 刚报出坐标的这一帧就是最后一�?—�?参数语义保持不变 */
      if (deadlineSec > totalSec)
        deadlineSec = totalSec;
    } else {
      printf("[CONT] #%lu 0.0,0.0\r\n", (unsigned long)rounds);
    }

    /* 定位成功导向：至今没有有效坐�?�?跟踪期限保底延长�?60 分钟�?
     * 继续按原间隔每轮上报（每�?+1 计数），直到定位成功或到上限 */
    if (!everFixed && (deadlineSec < GPS_CONT_MAX_SEC)) {
      deadlineSec = GPS_CONT_MAX_SEC;
      printf("[CONT] no fix, extend to 60 min\r\n");
    }

    /* upgps 跟踪不受工作时段/GPS 时段约束：云端主动指令，
     * 任何时间收到都执行到期限�?进入 GPS 时段一直无坐标、离开�?
     * 不强制再�?只约束自动重试路�?—�?OnMaster 开头已放弃重试�?*/

    elapsed = (HAL_GetTick() - startTick) / 1000u;   /* 毫秒 �?�?*/
    if (elapsed >= deadlineSec)
      break;                                   /* 期限已用�?*/

    remain = deadlineSec - elapsed;
    if (remain < (uint32_t)GPS_CONT_INTERVAL_SEC) {
      /* 剩下的时间凑不满一轮（一轮最短也�?GPS_CONT_INTERVAL_SEC 秒）�?
       * 不再空等，直接结束连续定位，回到正常定时调度（该休眠就休眠） */
      printf("[CONT] end\r\n");
      break;
    }

    /* 把本轮补齐到 GPS_CONT_INTERVAL_SEC 秒：搜星只用 10 秒也要占满，
     * 搜星耗时超过它则本轮自然以搜星耗时为准，不再额外等待�?
     * 期间不休眠，但必须持续跑 gpsTask()，否�?512 字节环形缓冲会堆满丢�?*/
    Safety_Begin(STAGE_TRACK_WAIT, (uint32_t)GPS_CONT_INTERVAL_SEC * 1000u + 2000u);
    while ((HAL_GetTick() - slotStart) <
           (uint32_t)GPS_CONT_INTERVAL_SEC * 1000u) {
      gpsTask();
      wdg_feed();
      HAL_Delay(100);
    }
  }

  if (everFixed)
    printf("[CONT] end: %lu rounds, had fix\r\n",
           (unsigned long)rounds);
  else
    printf("[CONT] end: %lu rounds, no fix\r\n",
           (unsigned long)rounds);

  return everFixed;
}

static void markPeriodicReport(void)
{
  periodicLastRtc = rtc_read_counter();
  periodicSeen = 1u;
}

/**
  * @brief  计算下次上报时间
  * @note   已对时：基于时隙调度，nextTx = 下一�?slot 边界（休眠时保持�?RAM�?
  *         未对时：nextTx = 当前 boot seconds + 间隔（重启后重新计算�?
  */
static void calcNextTxTime(void)
{
  uint32_t intervalSec = getConfigIntervalSec();
  uint32_t nowTs = getCurrentTimestamp();
  if (fastStartRounds > 0u && intervalSec > 60u) intervalSec = 60u;
  if (timeSynced) {
    uint32_t slotOffset = getSlotOffsetSec(intervalSec);
    uint32_t nowSec = nowTs % 86400u;
    uint32_t minWait = 1u, earliest, day, tod, slot;
    if (periodicSeen) {
      uint32_t elapsed = rtc_read_counter() - periodicLastRtc;
      if (elapsed < intervalSec / 2u) minWait = intervalSec / 2u - elapsed;
    }
    /* Nearest own slot at/after the minimum spacing, anchored to each day.
     * RTC elapsed is independent of forward/backward network time corrections. */
    earliest = nowSec + minWait;
    day = earliest / 86400u;
    tod = earliest % 86400u;
    slot = (tod / intervalSec) * intervalSec + slotOffset;
    if (slot < tod) slot += intervalSec;
    if (slot >= 86400u) { ++day; slot = slotOffset; }
    nextTxTimestamp = nowTs + day * 86400u + slot - nowSec;
  } else {
    nextTxTimestamp = nowTs + intervalSec;
  }
  printf("[SCHED] next=%lu wait=%lus\r\n", (unsigned long)nextTxTimestamp,
         (unsigned long)(nextTxTimestamp - nowTs));
}

/**
 * @brief  对时成功后的调度决策（卫星授�?/ SYN_TIME 两条路径共用�?
 * @note   首次开机场景：本机还没发过任何 LoRa 帧（tx_count==0）时�?
 *         开机的"立即上报"会被授时抢先重算掉（授时发生在首次上报之前，
 *         calcNextTxTime �?nextTxTimestamp 推到了一个周期之后）�?
 *         这里补救：立即补报一次时间帧，让云端第一时间拿到本机时间状态；
 *         上报�?OnMaster 末尾再正常消耗快速轮配额并计算休眠时间�?
 *         非首次（tx_count>0，如等待期收到对时）�?按原逻辑重算下次时间
 */
static void scheduleAfterSync(void)
{
  /* Do not charge a wall-clock jump as elapsed link time. */
  linkLastRtc = rtc_read_counter();

  if (tx_count == 0) {
    nextTxTimestamp = getCurrentTimestamp();   /* 立即 �?主循环下一圈就上报 */
    printf("[SCHED] first sync, report now\r\n");
  } else {
    calcNextTxTime();
  }
}

/**
  * @brief  �?configStr 解析上报间隔（秒�?
  * @note   configStr 格式: "30,0M,3t,6" �?第一个字�?30 = 30 分钟 = 1800 �?
  *         取值范�? 1~60 分钟
  */
static uint32_t getConfigIntervalSec(void)
{
  uint32_t min = (uint32_t)atoi(configStr);
  static uint8_t warned = 0;   /* 容错钳位只提示一次，避免刷屏 */

  /* 容错保护：上报周期必须在 1~60 分钟（工作时内的上报节奏不允许超�?1 小时）�?
   * A 指令写入端会拒绝超范围值，这里再兜底一层：
   * Flash 里的历史数据 / 手工改写 / 字段损坏都钳到合法范�?*/
  if (min == 0) {
    min = 1;                    /* 非数字或 0 �?�?1 分钟执行 */
    if (!warned) {
      warned = 1;
      printf("[CFG] period invalid(0/non-num), use 1 min\r\n");
    }
  } else if (min > 60) {
    if (!warned) {
      warned = 1;
      printf("[CFG] period %lu min >60, use 60\r\n",
             (unsigned long)min);
    }
    min = 60;
  }
  return min * 60;
}

/* ---- 时间段编码：base62 双字�?�?索引(0~275) �?当天整点区间 ----
 * 索引枚举当天所�?起点 < 终点"的整点区间：
 *   起点 0~22, 终点 起点+1 ~ 23 (23 点即代表 24 �? �?0~23 就是全天)
 *   总数 = 23+22+...+1 = 276, �?idx 0~275
 * 编号方式(三角枚举): idx = 前面所有起点的区间数之�?+ (终点 - 起点 - 1)
 * 实例(标准 base62 字典):
 *   "0M" �?idx 22  �?00:00 ~ 24:00
 *   "1M" �?idx 84  �?03:00 ~ 22:00
 *   "3t" �?idx 241 �?15:00 ~ 17:00
 *   "3u" �?idx 242 �?15:00 ~ 18:00
 * @warning 字典顺序必须与下发方完全一致，否则解出来的时间会偏 */
static const char TIME_DICT[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

#define TIME_HOUR_END  23   /* 终点最大取值：23 即代�?24:00 */

/**
  * @brief  �?configStr �?idx 段的原始字符串（到下一�?',' 为止，idx �?0 起）
  * @note   时间段字段是 "0M" 这种双字符编码，不能�?atoi（会被截断成 0�?
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
  * @brief  双字�?�?索引（base62 解码�?
  * @retval 0~275；字符不在字典中或长度不�?2 时返�?-1
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
 * @brief  校验配置里的时间段字段（工作时段 / GPS 时段）是否合�?
 * @param  f: 单个字段字符串（�?getConfigFieldStr 按逗号切出�?
 * @retval 1=合法, 0=非法
 * @note   合法 = 恰好 2 个字符，且都�?base62 字典内�?
 *         LoRa 空中乱码可能�?"0M" 变成 "0"�?0Mx"�?0?" 之类�?
 *         必须在写�?Flash 前拦下，否则时段判断永远失效还难以排�?
 */
static uint8_t configTimeFieldValid(const char *f)
{
  if (f == NULL)
    return 0;
  if (f[0] == '\0' || f[1] == '\0' || f[2] != '\0')
    return 0;                        /* �?/ 1 �?/ 3 位以�?都非�?*/
  if (strchr(TIME_DICT, f[0]) == NULL || strchr(TIME_DICT, f[1]) == NULL)
    return 0;                        /* 字符不在 base62 字典�?*/
  return twoCharToIndex(f) >= 0 && twoCharToIndex(f) <= 275;
}

/**
  * @brief  索引 �?当天整点区间（三角枚举的解码�?
  * @param[out] startHour 起点小时 0~22
  * @param[out] endHour   终点小时 起点+1~23�?3 表示 24:00�?
  * @retval 1=成功, 0=索引非法
  */
static uint8_t timeIdxToRange(int16_t idx, uint8_t *startHour, uint8_t *endHour)
{
  int16_t base = 0;
  uint8_t s;

  if (idx < 0 || idx > 275)
    return 0;

  for (s = 0; s < TIME_HOUR_END; s++) {
    int16_t cnt = (int16_t)(TIME_HOUR_END - s);   /* 该起点下的区间个�?*/

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
  * @brief  工作时间段索引（configStr �?2 段："0M" �?22�?
  */
static int16_t getConfigWorkStartIdx(void)
{
  char f[8];
  getConfigFieldStr(1, f, sizeof(f));
  return twoCharToIndex(f);
}

/**
  * @brief  GPS 自动上报时间段索引（configStr �?3 段："3t" �?241�?
  */
static int16_t getConfigGpsEndIdx(void)
{
  char f[8];
  getConfigFieldStr(2, f, sizeof(f));
  return twoCharToIndex(f);
}

/**
  * @brief  当前是否处于 GPS 工作时间段（configStr �?3 段，�?"3t" �?15:00~17:00�?
  * @retval 1=在时间段�? 0=不在 / 未对�?/ 字段非法
  * @note   未对时直接返�?0：此�?getTodaySecond() 是开机秒数，
  *         拿它判小时会落到错误时段，宁可不上报也不误唤�?GPS
  */
static uint8_t isInGpsWorkWindow(void)
{
  return isInGpsWorkWindowAt(getCurrentTimestamp());
}

/**
 * @brief  指定时刻是否处于 GPS 工作时间段（configStr �?3 段）
 * @param  ts: Unix 时间戳（秒）；用于提前判�?下次上报时刻"是否要搜�?
 * @retval 1=在时间段�? 0=不在 / 未对�?/ 字段非法
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
 * @brief  指定上报时刻是否�?必须准点"�?GPS 轮（类型 1�?
 * @param  ts: 下次上报�?Unix 时间�?
 * @retval 1=准点 GPS 轮（上轮失败重试 / 该时刻在 GPS 时间段内�?
 * @note   两类 GPS 轮的定位不同�?
 *         1) 类型 1（本函数返回 1）：定时上报�?*必须准点** —�?提前搜星�?
 *            到点无论有无坐标都发帧，时间点绝不允许后移；
 *         2) 类型 5（gpsWaitReport，upgps 指令触发）：**不受时间约束** —�?
 *            醒后现场搜星，搜多久算多久，上报时间可以后移到搜星结束，
 *            所以不参与提前量计算（返回 0�?
 */
/* Low battery only restricts GPS; communication is always allowed. */
static uint8_t gpsBatteryOk(void)
{
  return cachedBatteryPercent >= GPS_MIN_BATTERY_PCT;
}

static uint8_t gpsNeededTimelyAt(uint32_t ts)
{
  if (!gpsBatteryOk() || gpsWaitReport)
    return 0;                        /* 类型5：upgps 不受约束，现场搜 */
  return isInGpsWorkWindowAt(ts);
}

/* Deep-sleep preparation is bounded; a failed preparation must not be silent. */
static void loraSleep(void)
{
  if (!loraInitialized) return; /* Already asleep, or held in reset at boot. */
  if (rf_deepsleep() != RF_OK) Safety_Reset(RESET_RADIO);
  loraInitialized = 0u;
}

/* Link health: valid protocol traffic, monotonic work-time accumulation. */
#define WDG_SEG_SEC 3u

static void wdg_init(void) { Safety_Init(); }

static void wdg_feed(void) { Safety_Feed(); }


static void linkNoteValid(void)
{
  linkSilentSec = 0u;
  linkRecoveryStep = 0u;
  linkLastRtc = rtc_read_counter();
  Safety_MarkLinkHealthy();
}

/* Count only the overlap with the configured work window, including sleep. */
static uint32_t workElapsed(uint32_t endTs, uint32_t elapsed)
{
  uint8_t sh, eh;
  uint32_t beginTs, total = 0u;
  if (!timeSynced) return elapsed;
  if (!timeIdxToRange(getConfigWorkStartIdx(), &sh, &eh)) return 0u;
  if (eh == TIME_HOUR_END) eh = 24u;
  beginTs = endTs - elapsed;
  while (elapsed) {
    uint32_t tod = beginTs % 86400u;
    uint32_t part = 86400u - tod;
    uint32_t a, b;
    if (part > elapsed) part = elapsed;
    a = (tod > (uint32_t)sh * 3600u) ? tod : (uint32_t)sh * 3600u;
    b = (tod + part < (uint32_t)eh * 3600u) ? tod + part : (uint32_t)eh * 3600u;
    if (b > a) total += b - a;
    beginTs += part;
    elapsed -= part;
  }
  return total;
}

static void linkSilenceCheck(void)
{
  uint32_t now = rtc_read_counter(), elapsed;
  uint8_t enabled = !gpsContTotalMin;
  if (!linkPollReady) {
    linkPollReady = 1u;
    linkLastRtc = now;
    linkWasEnabled = enabled;
    return;
  }
  elapsed = now - linkLastRtc;
  if (!elapsed) return;
  linkLastRtc = now;
  if (elapsed > 86400u) Safety_Reset(RESET_RTC);
  if (enabled && linkWasEnabled)
    linkSilentSec += workElapsed(getCurrentTimestamp(), elapsed);
  linkWasEnabled = enabled;
  if (linkSilentSec < LINK_SILENCE_REBOOT_SEC) return;
  linkSilentSec = 0u;
  if (!linkRecoveryStep) {
    /* First recover just the radio; retry at the existing scheduled slot. */
    linkRecoveryStep = 1u;
    if (loraInitialized) loraSleep();
    loraInitialized = 0u;
    printf("[LINK] silence: radio reinit next slot\r\n");
  } else {
    Safety_Reset(RESET_LINK);
  }
}

/**
  * @brief  打印当前生效的两个时间段（工作时�?/ GPS 上报时间�?
  * @param  tag: 行首前缀，用于区分场景（启动�?/ 收到新配置时�?
  * @note   时段判断每次都是�?configStr 实时解析的，所以收�?A 指令更新配置�?
  *         **立即生效**，不存在"要重新加�?的问题，这里只是把结果打出来核对
  */
static void printTimeWindows(const char *tag)
{
  uint8_t s0, e0, s1, e1;
  if (timeIdxToRange(getConfigWorkStartIdx(), &s0, &e0) &&
      timeIdxToRange(getConfigGpsEndIdx(), &s1, &e1)) {
    printf("%s work=%u-%u GPS=%u-%u\r\n", tag,
           s0, (e0 == TIME_HOUR_END) ? 24u : e0,
           s1, (e1 == TIME_HOUR_END) ? 24u : e1);
  } else {
    printf("%s invalid window\r\n", tag);
  }
}

/**
  * @brief  �?deviceName 解析设备序号
  * @note   "v0-0" �?0, "v0-1" �?1, "v0-27" �?27
  *         格式: "vX-Y" �?'-' 后面的数�?
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
  * @brief  计算本设备在上报周期中的时隙偏移（秒�?
  * @note   公式: deviceIndex / deviceCount * intervalSec
  *         总台�?deviceCount 默认 30，可�?devicecount 指令下发�?~60，持久化�?
  *         �? v0-27, 30 �? 30 分钟周期 �?27/30*1800 = 1740 �?
  * @note   序号超出总台数时�?deviceCount 取模绕回，保证偏移仍落在本周期内�?
  *         否则 offset 会超�?intervalSec，时隙滑到下一个周期去
  */
static uint32_t getSlotOffsetSec(uint32_t intervalSec)
{
  uint8_t  index = getDeviceIndex();

  if (deviceCount == 0)          /* 防御：不会发生（加载/指令都已保证 >=1�?*/
    return 0;

  index = (uint8_t)(index % deviceCount);
  return (uint32_t)index * intervalSec / deviceCount;
}

/* ================= RTC：STOP 深度休眠的时间基�?=================
 * F1 �?RTC �?32 位秒计数器，STOP 模式下继续走，替�?HAL_GetTick 做系统时钟�?
 * 时钟源优�?LSE(32.768k, ±20ppm)；板上无晶振则退 LSI(40k, 误差~1%)�?
 * LSI 的偏差靠每轮上报后的 nettime 对时重新校准，不跨轮累计�?
 * STOP 唤醒�?= RTC 闹钟中断(EXTI line 17)；唤醒后系统时钟回落 HSI 8MHz�?
 * 必须重新 SystemClock_Config() 才能恢复 72MHz 和外设波特率 */
static uint32_t rtcPrl = 32767;  /* 预分�?1：LSE=32767，LSI=39999 */

/* wait (real ms, dog-fed) for flag bit(s) in an RCC reg to set.
 * REAL time base, not loop counts: loop counts scale with CPU speed
 * (288M loops ~= 28s @72MHz, far past the 10s IWDG - bit us hard) */
static uint8_t rcc_wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
  uint32_t start = HAL_GetTick();
  while (!(*reg & mask)) {
    if ((uint32_t)(HAL_GetTick() - start) >= ms) return 0u;
    wdg_feed();
  }
  return 1u;
}

/* �?RTC 写操作完成（RTOFF=1�?*/
static void rtc_wait_rtoff(void)
{
  uint32_t guard = 500000u;
  while (!(RTC->CRL & RTC_CRL_RTOFF) && --guard) { }
  if (!guard) Safety_Reset(RESET_RTC);
}

/* �?APB1 �?RTC 接口重新同步（RSF 置位�?
 * @note  系统复位�?STOP 唤醒后必须先�?RSF，否则读 RTC 计数得到 0�?*/
static void rtc_wait_rsf(void)
{
  uint32_t guard = 500000u;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard) { }
  if (!guard) Safety_Reset(RESET_RTC);
}

/* Re-sync APB shadow after STOP; high/low/high handles the carry race. */
static uint32_t rtc_read_counter(void)
{
  uint32_t high, low, high2;
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  (void)RTC->CRL;
  rtc_wait_rsf();
  high = RTC->CNTH;
  low = RTC->CNTL;
  high2 = RTC->CNTH;
  if (high != high2) low = RTC->CNTL;
  low |= high2 << 16;
  Safety_CheckRtc(low);
  return low;
}

/* �?RTC 标志（ALRF/SECF�?
 * @note  RM0008 要求：写 0 清标志后必须紧跟一次读操作�?
 *        且写前必须等 RTOFF=1（实测缺失尾读会导致 ALRF 后续无法置位�?*/
static void rtc_clear_alarm_flag(void)
{
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~(RTC_CRL_ALRF | RTC_CRL_SECF);
  (void)RTC->CRL;
}

/* 设置闹钟（绝�?RTC 秒）
 * @note  20+ 轮硬件实测结论（本芯片批次）�?
 *        1) ALRIE=1 会抑�?ALRF 置位�?EXTI17 事件，ALRIE 必须保持 0�?
 *           （ALRIE=0 �?ALRF 置位本身就能产生 EXTI17 事件�?
 *        2) 清标志必�?�?RTOFF �?�?0 �?尾读"，见 rtc_clear_alarm_flag
 *        3) STOP 唤醒不用 WFI+NVIC（实测不醒），用 WFE+EXTI17 事件，见
 *           enterStopUntil */
static void rtc_set_alarm(uint32_t alarmSec)
{
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;              /* 进入配置模式 */
  RTC->ALRH = (uint16_t)(alarmSec >> 16);
  RTC->ALRL = (uint16_t)(alarmSec & 0xFFFFU);
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;   /* 退出配置模�?*/
  (void)RTC->CRL;                       /* 尾读 */
  rtc_wait_rtoff();
  RTC->CRH &= (uint16_t)~RTC_CRH_ALRIE; /* 确保 ALRIE=0（见函数头注释） */
}

/* 确保所�?RTC 时钟源正在运行：
 * RTCSEL 在备份域（掉电保持），但 LSI 使能位在主电源域、每次上�?0�?
 * 若备份域已�?LSI 而本不上电没重开 LSI，RTC 将无时钟、计数恒 0 */
static void rtc_ensure_clock_running(void)
{
  if ((RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_LSE) {
    if (!rcc_wait_flag(&RCC->BDCR, RCC_BDCR_LSERDY, 500u)) Safety_Reset(RESET_RTC);
  } else {
    RCC->CSR |= RCC_CSR_LSION;
    if (!rcc_wait_flag(&RCC->CSR, RCC_CSR_LSIRDY, 500u)) Safety_Reset(RESET_RTC);
  }
}

/* RTC 初始化：备份�?+ 时钟�?+ 预分�?+ 闹钟中断
 * 备份域已配置过（软件复位/唤醒）则保留计数，不破坏时间 */
static void rtc_init(void)
{
  uint32_t before, start;
  Safety_Begin(STAGE_RTC, 8000u);
  /* RAM sync anchors are lost at reset; always rebuild RTC, keep diagnostics. */
  Safety_ResetBackupDomain();
  if (!Safety_UseLsi()) {
    RCC->BDCR |= RCC_BDCR_LSEON;
    if (rcc_wait_flag(&RCC->BDCR, RCC_BDCR_LSERDY, 4000u)) {
      rtcPrl = 32767u;
      RCC->BDCR |= RCC_BDCR_RTCSEL_LSE;
    }
  }
  if (!(RCC->BDCR & RCC_BDCR_RTCSEL)) {
    RCC->BDCR &= ~RCC_BDCR_LSEON;
    RCC->CSR |= RCC_CSR_LSION;
    if (!rcc_wait_flag(&RCC->CSR, RCC_CSR_LSIRDY, 500u)) Safety_Reset(RESET_RTC);
    rtcPrl = 39999u;
    RCC->BDCR |= RCC_BDCR_RTCSEL_LSI;
  }
  RCC->BDCR |= RCC_BDCR_RTCEN;
  rtc_ensure_clock_running();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  rtc_wait_rsf();
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->PRLH = (uint16_t)(rtcPrl >> 16);
  RTC->PRLL = (uint16_t)rtcPrl;
  RTC->CNTH = 0u;
  RTC->CNTL = 0u;
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  (void)RTC->CRL;
  rtc_wait_rtoff();
  RTC->CRH = 0u; /* Keep the board-tested ALRIE=0 WFE event path. */
  rtc_clear_alarm_flag();
  EXTI->PR = EXTI_PR_PR17;
  EXTI->RTSR |= EXTI_RTSR_TR17;
  EXTI->EMR |= EXTI_EMR_MR17;
  EXTI->IMR &= ~EXTI_IMR_MR17;
  /* An asserted RDY bit alone is insufficient: verify actual counter motion. */
  before = rtc_read_counter();
  start = HAL_GetTick();
  while (rtc_read_counter() == before) {
    if ((uint32_t)(HAL_GetTick() - start) >= 1600u) Safety_Reset(RESET_RTC);
    wdg_feed();
  }
  Safety_Begin(STAGE_MAIN, 15000u);
}

/* 进入 STOP 深度休眠，直到时间到�?wakeTs
 * @param wakeTs: Unix 时间戳；未对时时�?RTC 计数秒（两者此时同域）
 * @note  已对时后必须先把 Unix 时间戳换算回 RTC 计数�?
 *        （RTC 计数 �?Unix 时间戳，直接比较会把闹钟设到几十年后！）�?
 *        唤醒机制 = WFE + EXTI17 事件（实测本芯片 WFI+NVIC 唤醒不醒）；
 *        唤醒后时钟回�?HSI 8MHz、SysTick 停走�?
 *        由调用方判断到点�?SystemClock_Config() 恢复 */
static void enterStopUntil(uint32_t wakeTs)
{
  static uint8_t firstWindowDone = 0;
  uint32_t rtcNow, wakeRtc = wakeTs;

  /* Post-reset flashing window: hold awake 10s right before the FIRST
   * STOP entry so SWD can attach and re-flash (debug port is dead in
   * STOP). One-shot: later STOPs (and STOP wakes) skip it. Runs on every
   * entry path including the boot-fault backoff sleep. Two hard rules:
   *   1. feed the 10s IWDG throughout the wait (blind 10s delay = bite);
   *   2. finish BEFORE rtcNow is sampled below - a stale baseline makes
   *      the first STOP segment look like an RTC overrun (V2.2.6 bug:
   *      reset=RTC at stage=STOP, exactly 10s after the backoff print). */
  if (!firstWindowDone && !Safety_HadFault()) {
    uint32_t i;

    firstWindowDone = 1;
    Safety_Begin(STAGE_MAIN, 30000u);   /* re-arm deadline for the window */
    for (i = 0u; i < 100u; i++) {       /* 100 x 100ms = 10s, IWDG fed */
      wdg_feed();
      HAL_Delay(100u);
    }
  }
  rtcNow = rtc_read_counter();
  uint32_t savedCtrl, savedRadioIrq, savedGpsIrq;
  if (timeSynced) {
    uint32_t realDelta = wakeTs - syncedTimestamp;
    uint32_t localDelta = realDelta;
    if (driftConverged && rtcDriftPerHour && realDelta < 0x80000000u)
      localDelta = (uint32_t)((uint64_t)realDelta *
                   (uint32_t)(3600 + rtcDriftPerHour) / 3600u);
    wakeRtc = syncedRtcSec + localDelta;
  }
  if ((int32_t)(wakeRtc - rtcNow) <= 1) return;
  /* GPS is off and radio is asleep here. Noise must not keep waking the CPU. */
  savedRadioIrq = NVIC_GetEnableIRQ(EXTI2_IRQn);
  savedGpsIrq = NVIC_GetEnableIRQ(USART3_IRQn);
  HAL_NVIC_DisableIRQ(EXTI2_IRQn);
  HAL_NVIC_DisableIRQ(USART3_IRQn);
  savedCtrl = SysTick->CTRL;
  SysTick->CTRL = 0u;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  while ((int32_t)(wakeRtc - rtcNow) > 0) {
    uint32_t target = ((uint32_t)(wakeRtc - rtcNow) > WDG_SEG_SEC) ?
                       rtcNow + WDG_SEG_SEC : wakeRtc;
    uint32_t after;
    rtc_clear_alarm_flag();
    EXTI->PR = EXTI_PR_PR17;
    /* Drain stale events BEFORE arming, so an arriving alarm cannot be lost
     * by HAL's SEV/WFE/WFE sequence after the last deadline check. */
    __SEV();
    __WFE();
    rtc_set_alarm(target);
    Safety_PrepareStop();
    for (;;) {
      after = rtc_read_counter();
      if ((int32_t)(after - target) >= 0) break;
      MODIFY_REG(PWR->CR, PWR_CR_PDDS | PWR_CR_LPDS, PWR_CR_LPDS | PWR_CR_CWUF);
      SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
      __DSB();
      __WFE();
      __ISB();
      SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
      /* Spurious wake: no feed; RTC must reach the armed deadline first. */
    }
    if ((uint32_t)(after - rtcNow) > WDG_SEG_SEC + 2u) Safety_Reset(RESET_RTC);
    rtcNow = after;
    rtc_clear_alarm_flag();
    EXTI->PR = EXTI_PR_PR17;
    Safety_AfterStop();
  }
  SystemCoreClockUpdate(); /* STOP wakes on HSI; make SysTick valid at 8 MHz. */
  if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK) Safety_Reset(RESET_INIT);
  if (!(savedCtrl & SysTick_CTRL_TICKINT_Msk)) HAL_SuspendTick();
  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_2);
  HAL_NVIC_ClearPendingIRQ(EXTI2_IRQn);
  if (savedRadioIrq) HAL_NVIC_EnableIRQ(EXTI2_IRQn);
  if (savedGpsIrq) HAL_NVIC_EnableIRQ(USART3_IRQn);
  Safety_Begin(STAGE_MAIN, 15000u);
}

/**
  * @brief  获取当前"当天秒数"
  * @note   未同步时：把开机当�?00:00，返回开机秒�?
  *         已同步后：返回真实时间戳 % 86400
  */
static uint32_t getTodaySecond(void)
{
  return getCurrentTimestamp() % 86400;
}

/**
  * @brief  获取当前完整时间戳（基准 = RTC 秒计数器�?
  * @note   未同步时返回 RTC 计数本身（开机秒数）�?
  *         已同步后 = 对时时间�?+ RTC 走过的秒数（STOP 期间也连续）�?
  *         偏差收敛后（driftConverged）再做晶振补偿：本地走了 L 秒�?
  *         偏差 d �?小时时真实过�?L*3600/(3600+d) �?—�?本机时间
  *         回到真实轴，帧内时间�?时段判定/网格计算全部自动正确�?
  *         int64 中间量：RTC 累计可达数百万秒，�?600 会超 uint32
  */
static uint32_t getCurrentTimestamp(void)
{
  uint32_t localDelta, rtcNow = rtc_read_counter();
  if (!timeSynced) return rtcNow;
  localDelta = rtcNow - syncedRtcSec;
  if (driftConverged && rtcDriftPerHour)
    return syncedTimestamp + (uint32_t)((uint64_t)localDelta * 3600u /
                             (uint32_t)(3600 + rtcDriftPerHour));
  return syncedTimestamp + localDelta;
}

/**
  * @brief  判断闰年
  */
static uint8_t isLeapYear(uint16_t year)
{
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

/**
  * @brief  �?Unix 时间戳格式化�?"YYYY-MM-DD HH:MM:SS"
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
 * @brief  Unix 时间�?�?年份（用于对时有效性校验）
 * @note   ts=0 �?1970；uint32 最大值约对应 2106 年，循环不会�?
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
 * @brief  NMEA 坐标 ddmm.mmmm / dddmm.mmmm �?十进制度 ×1e5
 * @param  v:   原始字段，如 "2629.5800"
 * @param  dir: 'N'/'S'/'E'/'W'，南纬和西经取负
 * @retval �?×1e5（如 26.493 �?264930）；格式异常返回 0
 * @note   纯整数实现，避免拉入浮点解析库：度�?e5 = deg×1e5 + 分�?e4/6
 */
static int32_t nmeaToDeg(const char *v, char dir)
{
  const char *dot = strchr(v, '.');
  int32_t    deg = 0;
  uint32_t   minE4 = 0;   /* �?×1e4，如 29.58 �?�?295800 */
  uint32_t   mul = 100000;
  int32_t    d;

  if (dot == NULL || dot - v < 3)
    return 0;

  /* 整数部分去掉�?2 位即�?*/
  {
    uint8_t nd = 0;

    while (v < dot - 2) {
      /* >4 integer digits = garbled line: lat/lng max is 3 (180 deg),
       * reject to avoid int32 overflow on absurd EMI-noise input */
      if (*v < '0' || *v > '9' || ++nd > 4)
        return 0;
      deg = deg * 10 + (*v++ - '0');
    }
  }
  v = dot - 2;          /* 分钟整数部分 2 �?*/
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
 * @brief  �?×1e5 �?"[-]dd.ddddd" 字符串（等价�?%.5f 输出，补�?5 位小数）
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
 * @brief  解析 "26.52958" / " -26.52958" �?�?×1e5（跳过前导空格，带四舍五入）
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
          frac++;   /* �?6 位小数四舍五�?*/
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
 * @brief  �?NMEA 语句里第 n 个逗号分隔字段（n �?1 开始，即第 1 个逗号之后�?
 * @note   字段尾遇�?',' �?'*'（校验和起始）即结束
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
 * @brief  解析一�?NMEA 语句并打�?
 * @note   支持 GGA（定位质�?经纬�?海拔/卫星数）�?RMC（日�?速度�?
 *         $GPxxx / $GNxxx / $GBxxx 三种前缀都认，兼�?GPS/北斗/多模合一模块
 */
static void parseNmeaLine(char *line)
{
  /* @note 不再打印 NMEA 原始语句：搜星期间的观察改由 gpsPrintSearchStatus()
   *       每秒输出一行状态（坐标/年份/可靠�?卫星�?已搜时长/时间�?*/

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
      gpsFix.lat   = nmeaToDeg(lat, latDir[0]);
      gpsFix.lng   = nmeaToDeg(lng, lngDir[0]);
      gpsFix.sats  = (uint8_t)atoi(sats);
      gpsFixValid  = 1;
      gpsFixTick   = HAL_GetTick();   /* 刷新定位时间戳，供新鲜度判断 */
      (void)utc;                      /* 时间统一�?RMC 的，这里不用 */
    } else {
      gpsFixValid  = 0;
    }
  }
  /* ---- RMC: 日期、地面速度（GGA 里没有） ---- */
  else if (!strncmp(line, "$GPRMC", 6) || !strncmp(line, "$GNRMC", 6)) {
    char status[4], spd[12], date[8], utc[16];

    nmeaField(line, 1, utc, sizeof(utc));     /* HHMMSS.sss */
    nmeaField(line, 2, status, sizeof(status));
    nmeaField(line, 7, spd, sizeof(spd));
    nmeaField(line, 9, date, sizeof(date));   /* DDMMYY */

    /* 时间/日期无论定位与否都更新：未定位时 RMC 仍带 UTC 时间�?
     * "年份是否可信"这个判断就靠�?*/
    if (strlen(utc) >= 6) {
      gpsFix.hour   = (uint8_t)((utc[0] - '0') * 10 + (utc[1] - '0'));
      gpsFix.minute = (uint8_t)((utc[2] - '0') * 10 + (utc[3] - '0'));
      gpsFix.second = (uint8_t)((utc[4] - '0') * 10 + (utc[5] - '0'));
    }
    if (strlen(date) == 6) {
      gpsFix.day   = (uint8_t)((date[0] - '0') * 10 + (date[1] - '0'));
      gpsFix.month = (uint8_t)((date[2] - '0') * 10 + (date[3] - '0'));
      gpsFix.year  = (uint16_t)(2000 + (date[4] - '0') * 10 + (date[5] - '0'));
    }
    (void)status;   /* 速度/海拔不再解析（未使用），避免拉入 atof 浮点�?*/
    (void)spd;
  }
}

/**
 * @brief  当前是否持有"新鲜"的有效定位（供周期上报判断）
 * @note   光有 gpsFixValid 不够：若模块掉电或天线被遮挡导致不再输出 NMEA�?
 *         gpsFixValid 会一直停�?1，从而反复上报过期坐标�?
 *         故要求最�?GPS_FIX_TTL_MS 内刷新过一次�?
 * @retval 1=可上报定�? 0=应回退到原时间�?
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
 * @brief  GPS 任务：把环形缓冲里的字节拼成完整语句后解�?
 * @note   在主循环里调用；ISR 只负责往环形缓冲塞字节�?
 *         语句�?'\n' 结尾，遇�?'\r' 直接丢弃（NMEA �?CRLF）�?
 *         若语句超�?GPS_LINE_LEN 视为异常，丢弃并等待下一�?'\n' 重新同步�?
 */
static void gpsTask(void)
{
  /* 只在搜星(上电)期间处理 GPS 串口数据�?
   * 断电状态下模块本应静默；当前硬件存在人为上拉常供电�?
   * 这里统一丢弃字节，保证授�?定位解析只发生在搜星窗口�?
   * （之�?PB15 电源通路接好，断电即真静默，此逻辑天然兼容�?*/
  if (!gpsPowerState) {
    gpsRd = gpsWr;        /* 丢弃断电期间收到的所有字�?*/
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
        gpsLineLen = 0;  /* 超长异常，丢弃重�?*/
    }
  }

  /* 未定位时的心跳提示已取消：搜星期间的观察统一�?gpsPrintSearchStatus()
   * 每秒一行状态取代，不再�?等待定位...已收 xxx 字节" */

  /* 卫星授时兜底：本机还没对时且 GPS 给出可信日期 �?用一次卫星时�?
   * （能走到这里必然处于搜星窗口，授时天然只发生在搜星过程中�?*/
  gpsTrySyncFromSat();
}

/**
 * @brief  GPS 日期时间(UTC) �?Unix 时间戳（秒）
 * @note   仅用�?GPS 授时�?970-01-01 起算；年范围限制�?2026~2099 内无需优化
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
      days += 1;                    /* 闰年 2 �?+1 �?*/
  }
  days += (uint32_t)(day - 1);

  return days * 86400u + (uint32_t)hour * 3600u
         + (uint32_t)minute * 60u + (uint32_t)second;
}

/**
 * @brief  GPS 卫星授时兜底（只�?本机从未对时"时用一次）
 * @note   适用场景：设备上电后一直收不到集中�?云端下发对时（SYN_TIME），
 *         但这次搜到了�?—�?卫星时间就是可信源，直接拿来对一次时�?
 *         条件（全部满足才授时，任何一条不满足都直接返回）�?
 *           1) timeSynced == 0  —�?本机还没对过时；已对时后永不覆盖
 *              （服务器/微信端授时优先，卫星授时只是兜底�?
 *           2) gpsFixValid == 1 —�?本次已成功定位（避免拿无定位的乱日期�?
 *           3) year > 2025      —�?日期可信（模块冷启动默认日期�?2000 左右�?
 *         时区：RMC 输出 UTC�?8 小时得北京时间，�?SYN_TIME 时间戳同基准
 */
static void gpsTrySyncFromSat(void)
{
  uint32_t ts, nowTick;

  if (timeSynced)
    return;                         /* 已对时：不接收卫星授�?*/
  if (!gpsFixValid)
    return;                         /* 没定位：日期不可�?*/
  if (gpsFix.year <= 2025)
    return;                         /* 年份不对：模块日期未同步 */
  if (gpsFix.year > 2032)
    return;                         /* 年份越界：与对时校验规则一致，视为无效 */
  if (gpsFix.month < 1 || gpsFix.month > 12 || gpsFix.day < 1 || gpsFix.day > 31)
    return;                         /* 日期非法 */

  ts = gpsUtcToTimestamp(gpsFix.year, gpsFix.month, gpsFix.day,
                         gpsFix.hour, gpsFix.minute, gpsFix.second);
  ts += 8u * 3600u;                 /* UTC �?北京时间 */

  nowTick = rtc_read_counter();     /* RTC 秒（STOP 期间连续走） */

  /* �?SYN_TIME 首次对时一致：记录锚点，供后续晶振偏差计算 */
  anchorRtcSec    = nowTick;
  anchorTimestamp = ts;
  syncedTimestamp = ts;
  syncedRtcSec    = nowTick;
  timeSynced      = 1;

  {
    char cstBuf[20];

    /* Log the local time actually used for scheduling. */
    formatTimestamp(ts, cstBuf, sizeof(cstBuf));
    printf("[SYNC] GPS BJT=%s\r\n", cstBuf);
  }

  /* 对时成功后按真实时间重算下次上报时间（首次开机则立即补报一次） */
  scheduleAfterSync();
}

/**
 * @brief  阻塞等待"新鲜的有效定�?（用�?upgps 指令�?
 * @note   此函数运行期间主循环不跑，必须自己反复调�?gpsTask() 排空环形缓冲�?
 *         否则永远拿不到新解析结果 —�?这是本函数的核心，不能省
 * @param  minMs:     最短等待时间；即使一进来就有坐标也必须等够这么久
 *                    （给中继留够准备接收的时间，避免发太快丢帧）
 * @param  timeoutMs: 最长等待时间；HAL_GetTick 差值用无符号运算，
 *                    49 天回绕也能正确计�?
 * @retval 1=拿到有效定位, 0=超时仍未定位
 */
static void gpsPrintSearchStatus(uint32_t elapsedMs)
{
  char rtcBuf[24], latBuf[16], lngBuf[16];
  formatTimestamp(getCurrentTimestamp(), rtcBuf, sizeof(rtcBuf));
  deg1e5ToStr(gpsFix.lat, latBuf);
  deg1e5ToStr(gpsFix.lng, lngBuf);
  printf("[GPS] fix=%u %s,%s sat=%u t=%lus rx=%lu RTC=%s\r\n",
         (unsigned)gpsFixFresh(), latBuf, lngBuf, (unsigned)gpsFix.sats,
         (unsigned long)(elapsedMs / 1000u), (unsigned long)gpsRxCount, rtcBuf);
}

static uint8_t gpsWaitForFix(uint32_t minMs, uint32_t timeoutMs)
{
  if (!gpsBatteryOk()) return 0u;
  uint32_t startTick  = HAL_GetTick();
  Safety_Begin(STAGE_GPS, timeoutMs + 3000u);
  uint32_t elapsed;
  uint32_t lastStatus = 0;   /* 上次打印状态行的时刻（相对 startTick 的毫秒数�?*/

  printf("[GPS] search start, min %lus max %lus..\r\n",
         (unsigned long)(minMs / 1000), (unsigned long)(timeoutMs / 1000));

  if (gpsPrintEn)
    gpsPrintSearchStatus(0);   /* 进入就先给一行，之后每秒刷新 */

  while (1) {
    gpsTask();                    /* 排空环形缓冲并解�?NMEA */

    elapsed = HAL_GetTick() - startTick;

    /* �?1 秒刷新一行状态行（不再打�?NMEA 原始语句�?*/
    if (gpsPrintEn && ((elapsed - lastStatus) >= 1000)) {
      lastStatus = elapsed;
      gpsPrintSearchStatus(elapsed);
    }

    /* 先判"是否已满足上报条�?，再判超�?—�?
     * 否则最后一刻刚好定上位，会被超时分支覆盖成失败 */
    if (elapsed >= minMs && gpsFixFresh()) {
      printf("[GPS] fixed @%lus (min %lus), report\r\n",
             (unsigned long)(elapsed / 1000), (unsigned long)(minMs / 1000));
      return 1;
    }

    if (elapsed >= timeoutMs)
      break;

    wdg_feed();
    HAL_Delay(100);
  }

  printf("[GPS] search timeout(%lus), no fix\r\n",
         (unsigned long)(timeoutMs / 1000));
  return 0;
}

/**
  * @brief  指令目标校验：fields[1] 是否本机设备�?
  * @param  fields: 已按 '|' 切好的字段数组（fields[1]=目标, fields[2]=命令名）
  * @retval 1=是本机（执行指令�? 0=不是本机（已打印跳过原因�?
  * @note   统一替代每个指令分支里重复的
  *         if (strcmp(fields[1], deviceName) == 0) { ... }
  *         else { printf("[CMD] xx: dst=... != me=..., skip"); }
  */
static uint8_t cmdIsForMe(char *fields[])
{
  if (strcmp(fields[1], deviceName) != 0) {
    printf("[CMD] %s: dst=%s != me=%s, skip",
           fields[2], fields[1], deviceName);
    return 0;
  }
  linkNoteValid();
  return 1;
}

/**
  * @brief  解析接收到的消息（按 '|' 分割字段�?
  * @note   格式：MSG_TYPE|字段1|字段2|...|
  */
static void parse_rx_message(uint8_t *buf, uint8_t len)
{
  char *str = (char *)buf;
  char *fields[8] = {0};
  uint8_t field_count = 0;

  /* 收到任何 LoRa 数据（含乱码）都刷新链路活�?—�?哪怕解析失败，
   * 也说明中�?信道在工作，链路沉默看门狗不应对这种情况重启 */
  /* Health is refreshed only after protocol/target validation below. */

  fields[field_count++] = str;
  for (uint8_t i = 0; i < len && field_count < 8; i++) {
    if (str[i] == '|') {
      str[i] = '\0';
      if (i + 1 < len)
        fields[field_count++] = &str[i + 1];
    }
  }

  if (field_count < 2) {
    printf("[RX] invalid msg\r\n");
    return;
  }

  uint8_t msg_type = atoi(fields[0]);

  switch (msg_type) {
    case MSG_TYPE_SYN_TIME:
      /* 4|timestamp|NN|中结�?—�?验证码算法与 CENTEN 中继端完全一致：
       *   NN = timestamp 各位数字之和的个�?十位（数位和 % 100�? 位）
       *   �? 1794048626 �?数位�?47 �?"47"
       * 子设备用收到�?timestamp 重算比对，不匹配视为干扰�?损坏帧，
       * 直接丢弃本次对时（时间、锚点、晶振偏差全都不动） */
      if (field_count >= 3) {
        uint32_t recvTs = (uint32_t)atoi(fields[1]);
        uint32_t recvCode = (uint32_t)atoi(fields[2]);
        uint32_t digitSum = 0u, t = recvTs;
        uint32_t wantCode;
        uint32_t nowTick = rtc_read_counter();  /* RTC 秒（STOP 期间连续�?*/

        while (t) { digitSum += t % 10u; t /= 10u; }
        wantCode = digitSum % 100u;

        /* 验证码不匹配 �?拒绝本次对时 */
        if (recvCode != wantCode) {
          printf("[SYNC] code err(%02lu/%02lu), reject\r\n",
                 (unsigned long)recvCode, (unsigned long)wantCode);
          break;
        }

        /* 无效对时防护：年份必须在 1970~2032 之间，否则视为无效对时信息，
         * 不同步本机时间（已对时保持原样，未对时继续等有效值）�?
         * 覆盖场景：atoi 解析失败(=0�?970 之前按无效算不上,�?0 明显�?�?
         * 下发错误值、时间戳字段损坏�?*/
        if (recvTs == 0 || timestampYear(recvTs) > 2032u) {
          printf("[SYNC] ts=%lu year OOR, ignore\r\n",
                 (unsigned long)recvTs);
          break;   /* 丢弃这条对时，锚�?晶振偏差都不�?*/
        }

        /* 晶振偏差计算（首次对时设为锚点，后续每次对时输出偏差）�?
         * 本地间隔�?RTC 秒：STOP 休眠期间 RTC 继续走，测的才是真实钟差 */
        /* ���м�ʱ��Դ��飺��ʱ֡�� 4 �� = �·��м̵��м�����CENTEN ��
         * deviceName���� UID ���ɣ���̨��ͬ�������ζ�ʱ���Բ�ͬ�м� ��
         * ʱ�����ճ�ͬ�������� timeSynced ��"�״ζ�ʱ"·������ê�㣬
         * ƫ�����²�������̨�м�ʱ�ӻ�׼�в�������Ⱦ���񲹳�ֵ�� */
        {
          const char *rid = (field_count >= 4) ? fields[3] : "";
          if (lastSyncRelaySeen && strcmp(rid, lastSyncRelay) != 0) {
            printf("[Sync] relay %s->%s, re-anchor\r\n",
                   lastSyncRelay[0] ? lastSyncRelay : "?",
                   rid[0] ? rid : "?");
            timeSynced = 0;
          }
          strncpy(lastSyncRelay, rid, sizeof(lastSyncRelay) - 1);
          lastSyncRelay[sizeof(lastSyncRelay) - 1] = '\0';
          lastSyncRelaySeen = 1;
        }

        if (timeSynced) {
          uint32_t localElapsedSec = nowTick - anchorRtcSec;
          uint32_t remoteElapsedSec = recvTs - anchorTimestamp;
          /* 直接用间隔差值计算偏差，避免绝对时间戳整数截�?*/
          int64_t  driftSec = (int64_t)localElapsedSec - (int64_t)remoteElapsedSec;
          int32_t  driftPerHour = (remoteElapsedSec > 0)
              ? (int32_t)((int64_t)driftSec * 3600 / (int64_t)remoteElapsedSec)
              : 0;
          printf("[DRIFT] sample=%ds/h\r\n", (int)driftPerHour);

          /* 收敛判定（含异常剔除）：
           * �?区间过短(<10分钟)不采�?—�?上报周期可设（含 1 分钟快速轮），
           *   区间越短测量噪声越大�? 秒对时抖动在 1 分钟区间=360�?h�?
           *   会淹�?15�?h 收敛阈并误触剔除线；且短周期漂移累积本来就小
           *   �? 分钟�?3.6 秒），无需补偿。短区间对时仍校准时间轴�?
           *   刷新锚点，只是不产生偏差样本、不动补偿值；
           * �?偏差绝对值超 DRIFT_SAMPLE_MAX 视为中继自身授时错误，剔除；
           * �?连续两次可信样本�?�?DRIFT_CONVERGE_TOL 才启用补偿；
           * �?收敛后：小变化静默更新，大变化暂不采用、等下次对时确认 */
          if (localElapsedSec < 600u) {
            printf("[DRIFT] span %lus short, no sample\r\n",
                   (unsigned long)localElapsedSec);
          } else if (driftPerHour > DRIFT_SAMPLE_MAX ||
                     driftPerHour < -DRIFT_SAMPLE_MAX) {
            printf("[DRIFT] %ds/h abnormal, drop\r\n", (int)driftPerHour);
          } else if (!driftConverged) {
            int32_t diff = driftPerHour - lastDriftSample;

            if (diff < 0) diff = -diff;
            if (diff <= DRIFT_CONVERGE_TOL) {
              rtcDriftPerHour = driftPerHour;
              driftConverged  = 1;
              printf("[DRIFT] converged, comp %ds/h\r\n", (int)rtcDriftPerHour);
            } else {
              lastDriftSample = driftPerHour;
              printf("[DRIFT] sample=%ds/h unconverged\r\n", (int)driftPerHour);
            }
          } else {
            int32_t diff = driftPerHour - rtcDriftPerHour;

            if (diff < 0) diff = -diff;
            if (diff <= DRIFT_CONVERGE_TOL) {
              /* EMA smoothing: new=(3*cur+sample+2)/4, noise var /7,
               * no per-round alarm jump, tracking tau=4 syncs (2h) */
              rtcDriftPerHour = (3 * rtcDriftPerHour + driftPerHour + 2) / 4;
              printf("[DRIFT] ema comp %ds/h\r\n", (int)rtcDriftPerHour);
            } else
              lastDriftSample = driftPerHour;   /* 变化�? 暂不采用, 等下次确�?*/
          }
        } else {
          /* 首次对时：记录锚�?*/
          anchorRtcSec = nowTick;
          anchorTimestamp = recvTs;
          printf("[DRIFT] anchor\r\n");
        }

        /* 补偿残差验证：本�?含补�?时间 vs 中绷新下发对时的差值�?
         * 补偿正确时应接近 0~2 秒（链路延迟）——这就是补偿值的实时体检�?
         * 且每次对时（含类�?1 GPS 轮接收窗里的对时）都会用锚点间隔
         * 重测偏差并更新补偿值，补偿值随每次对时持续趋向正确 */
        if (timeSynced) {
          int32_t syncErr = (int32_t)(getCurrentTimestamp() - recvTs);
          printf("[SYNC] residual=%ds\r\n", (int)syncErr);
        }

        linkNoteValid();
        syncedTimestamp = recvTs;
        syncedRtcSec = nowTick;
        timeSynced = 1;

        {
          char tsBuf[24];

          /* 本机此刻的时间（含日期），对时完成后立即生效 */
          formatTimestamp(getCurrentTimestamp(), tsBuf, sizeof(tsBuf));
          printf("[SYNC] ts=%lu day=%lu now=%s\r\n",
                 (unsigned long)syncedTimestamp,
                 (unsigned long)getTodaySecond(), tsBuf);
        }
        /* 对时成功后重新计算下次上报时间（首次开机则立即补报一次） */
        scheduleAfterSync();
      }
      break;
    case MSG_TYPE_COM: /* 9: 指令  9|deviceId|cmd|value */
      if (field_count >= 3) {
        printf("[CMD] %s %s %s\r\n",
               fields[1], fields[2], field_count > 3 ? fields[3] : "");
        /* realyname: 9|v3-10|realyname|v3-11  �? 本机改名
         * @note 新名最�?5 字符（前缀2字符 + '-' + 2位序号，�?v3-23）：
         *       设备名嵌在所有上报帧里，长了会把帧推�?32 字节 */
        if (strcmp(fields[2], "realyname") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            if (strlen(fields[3]) > 5) {
              printf("[CMD] invalid value\r\n");
            } else {
              strncpy(deviceName, fields[3], sizeof(deviceName) - 1);
              deviceName[sizeof(deviceName) - 1] = '\0';
              saveDeviceNameToFlash(deviceName);
              printf("[CMD] name: %s\r\n", deviceName);
            }
          }
        }
        /* upgps: 9|v0-0|upgps|N  �? 等待有效定位后上�?GPS 坐标
         *   N=0（或参数非数字）：单次搜星上报一次就结束
         *   N>0                ：连续定�?N 分钟，每轮至�?GPS_CONT_INTERVAL_SEC �?
         * @note  这里只置标志，真正的等待/上报�?OnMaster() 末尾执行�?
         *        1) 阻塞搜星：最�?GPS_WAIT_MIN_MS(10�? 给中继留准备时间),
         *                    上限 GPS_WAIT_TIMEOUT_MS(4分钟)
         *        2) 首次�?5|deviceName|lat,lng|（MSG_TYPE_UP_GPS，定位失败发 "0.0,0.0"）；
         *           之后没拿到坐标的重试轮改�?1|...|（MSG_TYPE_GPS�?
         *        3) calcNextTxTime() 重新算下次时�?�?再进深度休眠
         *        不能在中�?解析上下文里长时间等待，否则会堵死整个流�?*/
        else if (strcmp(fields[2], "upgps") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            if (!gpsBatteryOk()) {
              printf("[GPS] low battery, skip\r\n");
              break;
            }
            int mins = atoi(fields[3]);        /* atoi 自动跳过前导空格 */

            if (mins < 0)
              mins = 0;
            if ((unsigned int)mins > (unsigned int)GPS_CONT_MAX_MIN) {
              mins = GPS_CONT_MAX_MIN;
              printf("[CMD] upgps: min OOR, clamp %d\r\n",
                     GPS_CONT_MAX_MIN);
            }

            gpsContTotalMin = (uint16_t)mins;
            gpsWaitReport = 1;

            if (mins > 0)
              printf("[CMD] upgps: cont %d min, >=%us/round\r\n",
                     mins, (unsigned int)GPS_CONT_INTERVAL_SEC);
            else
              printf("[CMD] upgps: single shot(0, max %lus)\r\n",
                     (unsigned long)(gpsSearchMaxMs / 1000u));
          }
        }
        /* sendmode: 9|v0-0|sendmode|1  �? 预留：原�?GPS 工作模式控制逻辑已移�?
         * （GPS 电源现在�?PB15 在搜星流程里自动控制�?
         * 保留本分支占位，后续在此追加新功�?*/
        else if (strcmp(fields[2], "sendmode") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            /* TODO: 后续功能在此实现 */
            printf("[CMD] sendmode=%d: reserved\r\n", atoi(fields[3]));
          }
        }
        /* devicecount: 9|v0-0|devicecount|30  �? 设置分时隙上报的子设备总台�?
         * 时隙偏移 = 序号 / 总台�?* 上报周期，默�?30 �?
         * @note 范围 1~60：周期最�?1 分钟(60�?�?0 台刚好每�?1 秒时�?
         * @note 改了台数后时隙立刻重排（每台设备各自收到指令才改），
         *       要在所有设备上广播同一条指令，否则各台的时隙划分会不一�?*/
        else if (strcmp(fields[2], "devicecount") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int cnt = atoi(fields[3]);   /* atoi 会自动跳过前导空�?*/

            if (cnt >= 1 && cnt <= 60) {
              deviceCount = (uint8_t)cnt;
              saveDeviceCountToFlash(deviceCount);
              printf("[CMD] devcnt=%d, slot offset -> %lus\r\n",
                     deviceCount,
                     (unsigned long)getSlotOffsetSec(
                         ((fastStartRounds > 0) && (getConfigIntervalSec() > 60u))
                             ? 60u : getConfigIntervalSec()));
            } else {
              printf("[CMD] invalid value\r\n");
            }
          }
        }
        /* readinfo: 9|v0-0|readinfo|[value]  �? 上报设备信息帧（每次一条，帧长�?2字节�?
         * value: 0/缺省/乱码=设备概况  1=晶振偏差  2=最近收�?RSSI/SNR */
        else if (strcmp(fields[2], "readinfo") == 0 && field_count >= 3) {
          if (cmdIsForMe(fields)) {
            readInfoValue = (field_count >= 4) ? (uint8_t)atoi(fields[3]) : 0u;
            readInfoNeedReport = 1;   /* 触发确认�?[设备信息] 发送日�?*/
          }
        }
        /* txpower: 9|v3-3|txpower|15  �? 动态修改发射功�?
         * @note 范围 10~20 dBm（保证接收范围下�?模块上限）；乱码 atoi=0 也被�?*/
        else if (strcmp(fields[2], "txpower") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int newPwr = atoi(fields[3]);

            if (newPwr < 10 || newPwr > 20) {
              printf("[CMD] invalid value\r\n");
            } else {
              currentTxPower = (uint8_t)newPwr;
              rf_set_tx_power((uint8_t)newPwr);
              saveTxPowerToFlash((uint8_t)newPwr);
              printf("[CMD] pwr set: %d dBm\r\n", newPwr);
            }
          }
        }
        /* C: 9|v0-0|C|26.52958,109.39087  �? 设置农场中心坐标并持久化 */
        else if (strcmp(fields[2], "C") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int32_t la = 0, lo = 0;
            const char *comma = strchr(fields[3], ',');
            /* 整数解析（�?e5），精度�?%.5f；parseDeg1e5 会跳过前导空�?
             * （下发值形�?" 26.52,109.39"），第二段以后的内容忽略 */
            if (comma != NULL &&
                parseDeg1e5(fields[3], &la) &&
                parseDeg1e5(comma + 1, &lo) &&
                centerCoordValid(la, lo)) {
              char laBuf[16], loBuf[16];
              /* 归一化成 5 位小数存储：帧长固定，且天然不含会破坏帧结构�?'|' */
              deg1e5ToStr(la, laBuf);
              deg1e5ToStr(lo, loBuf);
              snprintf(centerStr, sizeof(centerStr), "%s,%s", laBuf, loBuf);
              centerLat1e5 = la;      /* ͬ����ֵ���棺GPS ���ƫ���� */
              centerLng1e5 = lo;
              centerValid  = 1;
              saveCenterToFlash(centerStr);
              printf("[CMD] center saved: %s\r\n", centerStr);
            } else {
              /* 非法：不写内存、不�?Flash，保留上一次的合法�?*/
              printf("[CMD] C: invalid, keep %s: %s\r\n",
                     (centerStr[0] != '\0') ? centerStr : "(未设�?", fields[3]);
            }
            /* 无论本次是否合法都上报当前坐标：
             * 合法→回新坐标；非法→回上一个合法值（未设过则 "0,0"�?*/
            centerNeedReport = 1;
          }
        }
        /* A: 9|v3-17|A|10,0M,3t,6  �? 更新配置并标记上�?*/
        else if (strcmp(fields[2], "A") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            /* 合法性检�?1：时间段字段（第 2/3 段）必须�?2 �?base62 字符
             * （LoRa 乱码防护：位数不对或字符越界一律拒绝，不写�?Flash�?*/
            char wf[8], gf[8];

            getConfigFieldStrFrom(fields[3], 1, wf, sizeof(wf));
            getConfigFieldStrFrom(fields[3], 2, gf, sizeof(gf));

            /* 合法性检�?2：上报周期必须在 1~60 分钟之间 */
            uint32_t newMin = (uint32_t)atoi(fields[3]);

            if (!configTimeFieldValid(wf) || !configTimeFieldValid(gf)) {
              /* 中文值不写三元表达式（ARMCC5 GBK 吞引号），用 if/else */
              printf("[CMD] invalid value\r\n");
              printf("[CMD]   work=\"%s\" gps=\"%s\"", wf, gf);
            } else if (newMin >= 1 && newMin <= 60) {
              strncpy(configStr, fields[3], sizeof(configStr) - 1);
              configStr[sizeof(configStr) - 1] = '\0';
              saveConfigToFlash(configStr);
              configNeedReport = 1;  /* 标记需要上报配�?*/
              printf("[CMD] cfg updated: %s\r\n", configStr);

              /* 工作时间�?/ GPS 上报时间段都是从 configStr 实时解析的，
               * 这里不用做额外的"重新加载"，直接把生效的区间打出来核对 */
              printTimeWindows("[指令] 新时�?: ");
            } else {
              printf("[CMD] invalid value\r\n");
            }
          }
        }
        /* relayreboot: 9|v0-0|relayreboot|  �? 远程重启设备
         * @note NVIC_SystemReset() 真复位（等同断电重启）：
         *       RAM 全部清零（fastStartRounds/对时状�?重试标志都回到初始）�?
         *       不是 STOP 深度休眠那种"睡一觉继�?，重新走完整开机流程：
         *       快速上�?3 �?�?等对�?�?恢复正常调度 */
        else if (strcmp(fields[2], "relayreboot") == 0) {
          if (cmdIsForMe(fields)) {
            printf("[CMD] relayreboot, resetting\r\n");
            HAL_Delay(100);            /* 让日志从串口发完 */
            Safety_Reset(RESET_REMOTE);
          }
        }
        /* searchtime: 9|dev|searchtime|N - GPS search max N minutes (1~10),
         * saved to flash, applied immediately (presearch/tracking follow it) */
        else if (strcmp(fields[2], "searchtime") == 0 && field_count >= 4) {
          if (cmdIsForMe(fields)) {
            int newMin = atoi(fields[3]);

            if (newMin >= GPS_SEARCH_MIN_MIN && newMin <= GPS_SEARCH_MAX_MIN) {
              gpsSearchMaxMs = (uint32_t)newMin * 60000UL;
              saveSearchTimeToFlash(gpsSearchMaxMs);
              printf("[CMD] searchtime: %d min\r\n", newMin);
            } else {
              printf("[CMD] invalid value\r\n");
            }
          }
        } else {
          printf("[CMD] unknown: %s\r\n", fields[2]);
        }
      }
      break;
    default:
      printf("[RX] type=%d\r\n", msg_type);
      break;
  }
}

/**
  * @brief  开�?3 秒接收窗口（single 模式，最多只收一条），收到则解析
  * @note   抽成函数的原因：MSG_TYPE_GPS(1) 上报之后还要再开一次窗口，
  *         保证节点报完定位后仍处于可被下发指令的状态；
  *         �?MSG_TYPE_UP_GPS(5) 的首次跟踪上报后不再开窗口
  */
static void openRxWindow3000(void)
{
  Safety_Begin(STAGE_RX, 6000u);
  rf_set_recv_flag(RADIO_FLAG_IDLE);
  if (rf_enter_single_timeout_rx(3000u) != RF_OK) Safety_Reset(RESET_RX);
  if (!windowPrintOff)
    printf("[RX] 3s listen\r\n");

  /* 等待接收完成或超时（芯片�?3 �?RX 超时本应产生 RX_TIMEOUT 中断置标志；
   * 若模块半�?中断丢失标志停在 IDLE�? 秒兜底超时复位自愈，防整机卡死） */
  uint32_t rxStart = HAL_GetTick();
  while (rf_get_recv_flag() == RADIO_FLAG_IDLE) {
    wdg_feed();   /* RX window wait: feed inside the loop */
    if (HAL_GetTick() - rxStart > 5000u) {
      Safety_Reset(RESET_RX);
    }
  }

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
    /* 记录最近收包质量：readinfo value=2 上报�?*/
    lastRxRssi = local_rssi;
    lastRxSnr  = local_snr;
    lastRxSeen = 1;
    if (local_size > RX_LEN) local_size = RX_LEN;
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i];
    __enable_irq();

    if (local_size < RX_LEN)
      local_buf[local_size] = '\0';
    else
      local_buf[RX_LEN - 1] = '\0';

    if (!windowPrintOff)
      printf("[RX] len=%d rssi=%d snr=%d data=%s\r\n",
             local_size, local_rssi - 256, local_snr, (char *)local_buf);

    /* 解析消息 */
    parse_rx_message(local_buf, local_size);
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);   /* 空窗是常�? 不打�?�?Flash) */
  } else if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    if (!windowPrintOff)
      printf("[RX] CRC err\r\n");
  }
  Safety_Begin(STAGE_MAIN, 15000u);
}

/**
  * @brief  把待回的包发出去（配置帧 / 农场中心坐标帧）
  * @note   收到 A(配置) �?C(中心坐标) 指令后只是置位标记，真正回包在这里做�?
  *         要在"每个接收窗口之后"都调用一次：普通轮的窗口在定时帧之后�?
  *         GPS 轮的窗口�?MSG_TYPE_GPS 之后，位置不同；
  *         否则末尾窗口里收到的设置指令要拖到下一轮才回包
  */
/* =====================================================================
 * @brief  boot reset report frame: 8|dev|rst:CAUSE|cnt| (type 8)
 * @note   sent once per reboot, first pending-report slot of the first
 *         window; cloud sees every reboot incl. watchdog/SW resets
 * ===================================================================== */
static void sendResetInfoMessage(void)
{
  char tx_msg[TX_LEN];

  txCountNext();
#if TX_COUNT_ENABLE
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|rst:%s-%u|%lu|",
           MSG_TYPE_READINFO, deviceName, bootRstCause, (unsigned)Safety_LastStage(),
            (unsigned long)tx_count);
#else
  snprintf(tx_msg, sizeof(tx_msg), "%d|%s|rst:%s-%u|",
           MSG_TYPE_READINFO, deviceName, bootRstCause, (unsigned)Safety_LastStage());
#endif
  loraTx(tx_msg);
}

static void reportPendingMessages(void)
{
  /* 配置需要上报时，延�?1 秒发送配置消息（开机首次或收到配置更新指令�?*/
  /* reboot report goes first: 8|dev|rst:CAUSE|cnt| (once per boot) */
  if (bootRstPending) {
    HAL_Delay(1000);
    sendResetInfoMessage();
    bootRstPending = 0;
  }

  if (configNeedReport) {
    HAL_Delay(1000);
    sendConfigMessage();
    configNeedReport = 0;  /* 标记已上�?*/
  }

  /* 收到 readinfo 指令后，延迟 1 秒上报设备信�?8|deviceName|台数|功率|保留| */
  if (readInfoNeedReport) {
    HAL_Delay(1000);
    sendReadInfoMessage();
    readInfoNeedReport = 0;  /* 标记已上�?*/
  }

  /* 收到 C 指令后，延迟 1 秒上报农场中心坐�?1|deviceName|lat,lng| */
  if (centerNeedReport) {
    HAL_Delay(1000);
    sendCenterMessage();
    centerNeedReport = 0;  /* 标记已上�?*/
  }
}

static void OnMaster(void)
{
  char tx_msg[TX_LEN];
  uint32_t today_sec = getTodaySecond();
  cachedBatteryPercent = GetBatteryPercent();

  /* 上轮失败的重试只�?工作时间段内"继续：已离开工作时间段就放弃重试�?
   * 否则会出�?跳过定时帧、又不搜�?的空转轮次，设备整轮什么都不上�?
   * @note 未对�?timeSynced=0)时两个时段判断都恒为 0，此�?*不能**放弃重试�?
   *       否则一开机没对时就会把重试标记清掉，永远拿不到坐标�?
   *       未对时一律继续重试，等到对上时间后再按时段判�?*/

  /* 下列两种情况不发定时�?MSG_TYPE_TIME，把这一轮让�?GPS�?
   *   1) 上轮无坐标要重试(gpsNeedRetry)
   *   2) 当前�?GPS 工作时间段内
   *      —�?本轮改由末尾�?MSG_TYPE_GPS 上报，并在它上报后开接收窗口�?
   *         对时与接收下发指令都在那个窗口完成（故本处不再开窗口�?
   * @note 本轮已收�?upgps 指令(gpsWaitReport=1)时不跳过：末尾走的是 UP_GPS
   *       老逻辑（上报后不开窗口），必须靠定时后这个窗口收指�?对时 */
  uint8_t skipTimeFrame = (gpsBatteryOk() && isInGpsWorkWindow()) ? 1U : 0U;

  if (skipTimeFrame) {
    /* GPS work window: the MSG_TYPE_GPS at the end replaces the time frame */
  } else {
    /* 组包�?|v0-0|当天秒数|电量[|计数]
     * 周期上报固定发时间帧，不管有没有定位 —�?定位�?UPGPS 流程单独触发上报 */
    txCountNext();   /* 每发一�?LoRa 计数 +1 */
#if TX_COUNT_ENABLE
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%lu|%d|%lu",
             MSG_TYPE_TIME, deviceName,
             (unsigned long)today_sec, cachedBatteryPercent, (unsigned long)tx_count);
#else
    snprintf(tx_msg, sizeof(tx_msg), "%d|%s|%lu|%d|",
             MSG_TYPE_TIME, deviceName,
             (unsigned long)today_sec, cachedBatteryPercent);
#endif

    loraTx(tx_msg);
    markPeriodicReport();
  }

  /* ---- 3 秒接收窗口（single 模式，最多只收一条） ----
   * 只在本轮发了定时帧时在这里开；跳过定时帧的轮次改为在末尾 MSG_TYPE_GPS
   * 上报之后再开（见下面 GPS 块），避�?没发帧还白等 3 �? */
  if (!skipTimeFrame)
    openRxWindow3000();

  /* 窗口里收到的 A/C 等设置指令，本轮就回�?*/
  reportPendingMessages();

  /* ---- GPS 上报流程 ----
   * 规则（GPS 工作时间段一定落在工作时间段之内�?
   *   (1) upgps 指令(gpsWaitReport) —�?任何时间收到都立刻搜星，
   *       �?MSG_TYPE_UP_GPS(5) 上报，上报后**不开**接收窗口
   *   (2) 上轮 MSG_TYPE_UP_GPS 没拿到正确坐�?gpsNeedRetry) �?
   *       之后每个上报点都�?MSG_TYPE_GPS(1) 重试并开接收窗口�?
   *       直到拿到正确坐标为止
   *       （仅 GPS 工作时段内重试，离开 GPS 时段即放弃，�?OnMaster 开头）
   *   (3) GPS 工作时间�?config �?3 �? �?—�?正常�?MSG_TYPE_GPS(1) 上报
   *       并开接收窗口；已有新鲜坐标就直接发缓存坐标，没有坐标才临时搜�?
   * 放在 calcNextTxTime() 之前 —�?等待可能耗时几十秒，
   * 之后必须重新计算下次上报时间，不能用本轮算出的旧值�?*/
  {
    uint8_t inGpsWin = isInGpsWorkWindow();

    if (!gpsBatteryOk()) {
      printf("[GPS] low battery, skip\r\n");
      gpsWaitReport = 0u;
      gpsContTotalMin = 0u;
      gpsPreSearched = 0u;
      gpsPreFixOk = 0u;
    }
    if (gpsBatteryOk() && (gpsWaitReport || inGpsWin)) {
      uint8_t firstCmd = gpsWaitReport;        /* upgps 指令触发的第一�?*/
      uint8_t useType1 = firstCmd ? 0U : 1U;   /* 0=UP_GPS(5), 1=GPS(1) */

      gpsWaitReport = 0;  /* 先清零，避免异常路径残留导致每轮都在这儿�?*/



      /* 类型5(upgps) 不受时间约束：一律现场搜星，搜完再发（时间可后移）�?
       * 万一有残留的预搜标志（例如上一轮被指令打断），在这里清掉，避免误用 */
      if (firstCmd && gpsPreSearched) {
        gpsPreSearched = 0;
        gpsPreFixOk    = 0;
      }

      if (gpsPreSearched) {
        /* 【提前搜星】本轮在唤醒时已预先搜完（搜�?�?窗口不足被跳过）�?
         * 直接取结果上报，绝不再现场搜�?—�?上报时间点因此不会后移�?
         * 预搜成功�?GPS 已断电，坐标暂存�?gpsFix 里（gpsPowerOff 不清坐标�?*/
        uint8_t preOk = gpsPreFixOk;

        /* 中文值不要写进三元表达式：ARMCC5 �?GBK 解析会吞掉引�?*/
        if (preOk)
          printf("[GPS] pre: fixed(%lus)\r\n", (unsigned long)gpsPreSec);
        else
          printf("[GPS] pre: nofix(%lus)\r\n", (unsigned long)gpsPreSec);

        sendGpsPositionMessage(preOk, useType1, gpsPreSec, gpsPreBytes);

        gpsPreSearched = 0;
        gpsPreFixOk    = 0;
        gpsPowerOff();   /* 确保断电（预搜里已断电，重复调用安全�?*/
      } else {

        /* 兜底路径（未走预搜，如运行中被现场触发）�?
         * 这一轮要�?GPS：PB15 置低上电，用完立刻置高断�?
         * @note 一律重新搜星，不发缓存坐标�?
         *       缓存里的坐标可能是很久之前某一轮拿到的，不能代表当前位置，
         *       所以即�?gpsFixFresh() 为真也要重新搜一遍再上报 */
        gpsPowerOn();

        /* GPS 打印的开关与恢复�?gpsSearchAndReport() 内部逐轮处理 */
        if (gpsContTotalMin > 0) {
          /* 连续定位模式（upgps|N 跟踪）：时长内不休眠�?
           * �?GPS_CONT_INTERVAL_SEC 秒上报一次坐标；
           * 无坐标时跟踪期限自动保底延长�?60 分钟（定位成功导向）�?
           * 跟踪结束：成�?�?恢复定时上报；失�?�?只保�?*一�?*
           * 类型 1 重试机会（下轮对时点搜星上报一次），这次仍无坐�?
           * 就彻底清零不再延�?—�?60 分钟+1 次重试都失败说明环境
           * 真无信号，无限延续只是每轮白�?4 分钟搜星�?*/
          (void)gpsRunContinuous(gpsContTotalMin, useType1);

          linkLastRtc = rtc_read_counter();
          gpsContTotalMin = 0;
          printf("[GPS] tracking end\r\n");
        } else {
          /* 单次搜星上报（参数为 0 �?upgps 指令，或失败后的自动重试�?*/
          (void)gpsSearchAndReport(useType1);   /* one search, one report - done either way */
        }

        /* GPS 用完：PB15 置高断电
         * （连续定位模式是整段结束才断电，避免反复上电/断电�?*/
        gpsPowerOff();
      }

      /* MSG_TYPE_GPS(1) 上报后开接收窗口（对�?+ 收下发指令）�?
       * MSG_TYPE_UP_GPS(5) 的首次跟踪上报不开窗口，保持老逻辑 */
      if (useType1) {
        openRxWindow3000();

        /* 这个窗口里收到的指令照常处理：upgps �?�?gpsWaitReport 下一轮执行；
         * A/C 等设�?�?本轮立即回包，不等下一�?*/
        reportPendingMessages();
      }

      /* 注意：分支提示不要写�?one-liner 三元 + 中文值，
       * 中文紧贴闭合引号会被 ARMCC5 �?GBK 吞掉那个引号（missing closing quote），
       * 故改�?if/else + �?\r\n 收尾（与文件其它 printf 风格一致） */

    }
  }

  /* 开机前 5 轮快速上报：本轮已上报完成，消耗一次配�?
   * （先递减再算下次时间：轮1~�? 都是 5 分钟，轮6 起恢复配置周期） */
  if (fastStartRounds > 0)
    fastStartRounds--;

  /* 计算下次上报时间（配置更新后间隔可能变化�?*/
  calcNextTxTime();

  /* 判断是否需要进入深度休�?*/
  {
    uint32_t nowTs = getCurrentTimestamp();
    uint32_t waitSec = (nextTxTimestamp > nowTs) ? (nextTxTimestamp - nowTs) : 0;
    char     nextBuf[24];

    /* 下次发送的绝对时间点（HH:MM:SS）�?
     * 格式化结果是 "YYYY-MM-DD HH:MM:SS"�?11 直接指向时分秒；
     * 未对时时�?1970 起递增的本机时钟，一样能看出节奏 */
    formatTimestamp(nextTxTimestamp, nextBuf, sizeof(nextBuf));

    if (waitSec > 10) {
      /* 下次上报 > 10 秒后 �?PAN3029 进入深度休眠省电 */
      printf("[SLEEP] deep %lum%lus, %s\r\n",
             (unsigned long)(waitSec / 60u), (unsigned long)(waitSec % 60u),
             nextBuf + 11);
      loraSleep();
      loraInitialized = 0;  /* 标记需要唤醒后重新初始�?*/
    } else {
      printf("[ACTIVE] LoRa ready, tx in %lum%lus, %s\r\n",
             (unsigned long)(waitSec / 60u), (unsigned long)(waitSec % 60u),
             nextBuf + 11);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  应用程序主入口函�?
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/
  wdg_init();
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

  strncpy(bootRstCause, Safety_BootCause(), sizeof(bootRstCause) - 1u);
  bootRstPending = 1u;
  printf("[BOOT] reset=%s stage=%u clock=%s\r\n", bootRstCause,
         (unsigned)Safety_LastStage(), clockFallback ? "HSI64" : "HSE72");
  rtc_init();
  {
    uint32_t backoff = Safety_BackoffSeconds();
    if (backoff) {
      printf("[BOOT] repeated fault, backoff %lus\r\n", (unsigned long)backoff);
      /* Recover a retained TX/RX state before the long fault backoff. If the
       * module cannot initialize, hold reset asserted to inhibit activity. */
      if (!loraTryInit() || rf_deepsleep() != RF_OK)
        HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
      loraInitialized = 0u;
      enterStopUntil(getCurrentTimestamp() + backoff);
      SystemClock_Config();
    }
  }


  /* �?Flash 加载设备名、配置、发射功率、农场中心坐标和设备台数 */
  if (!NvStore_Recover()) Safety_Reset(RESET_FLASH);
  loadDeviceNameFromFlash();
  loadConfigFromFlash();
  loadTxPowerFromFlash();
  loadSearchTimeFromFlash();
  loadCenterFromFlash();
  loadDeviceCountFromFlash();
  checkVersionReportFlag();   /* new firmware? -> one readinfo x6 cycle */

  /* 打印启动信息 */
  printf("\r\n[BOOT] LoRa\r\n");
  printf("dev: %s\r\n", deviceName);
  printf("cfg: %s\r\n", configStr);
  printf("slot=%u total=%u\r\n", getDeviceIndex(), deviceCount);
  printTimeWindows("时段  : ");   /* 工作时间�?+ GPS 上报时间�?*/

  /* 首次 TX 延迟到主循环中执行（LoRa 在首�?TX 前才初始化） */
  nextTxTimestamp = 0;  /* 首次立即触发 TX */

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE BEGIN 3 */
    uint32_t nowTs;
    Safety_Begin(STAGE_MAIN, 15000u);
    nowTs = getCurrentTimestamp();
    linkSilenceCheck();

    if (loraInitialized) {
      /* 链路沉默看门狗：工作时间�?2 小时没收到任�?LoRa 信息 �?真复位�?
       * 活跃期间每圈(100ms)查一次，开销可忽略；深度休眠期间 RTC 时间�?
       * 照样连续计时，醒来必过这里，所以休眠多久都不会漏检 */


      /* LoRa 已激活：检查是否到达发送时�?*/
      if (nowTs >= nextTxTimestamp) {
        OnMaster();  /* 内部会处理发送、接收窗口、以及发送后的休眠判�?*/
      }
    } else {
      /* LoRa 未初始化（首次启�?�?�?deepsleep 唤醒�?
       * 唤醒时刻�?本轮是否必须准点�?GPS �?决定�?
       *   类型1(GPS时段/重试) �?提前搜星，到点准点发帧（提前量见下）
       *   类型5(upgps 指令)   �?提前 5 秒醒，醒后现场搜星，上报时间可后�?
       *   普通定时帧          �?提前 5 秒醒（LoRa 初始化倒计时）
       *
       * 提前�?= min(搜星上限+5, 周期-5)�?
       *   正式上线周期(30~60 分钟) > 搜星上限，取 245 秒，搜星时间充足�?
       *   若周期被设得很小(�?1 分钟 < 4 分钟搜星上限)，压缩为"周期-5 �?�?
       *   保证提前量不跨进上一轮，绝不会把这个上报点挤掉；
       *   压缩后窗口不�?GPS_WAIT_MIN_MS(10 �? �?gpsPreSearch() 会跳过搜星，
       *   到点照发 "0.0,0.0|字节数|" —�?准点优先，绝不后�?*/
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
        /* STOP 醒来时钟回落 HSI 8MHz，先恢复 72MHz（SysTick 随之恢复�?*/
        SystemClock_Config();
        /* Battery gates GPS only, regardless of the number of uplinks. */
        cachedBatteryPercent = GetBatteryPercent();
        doPreSearch = gpsNeededTimelyAt(nextTxTimestamp) &&
                      !gpsPreSearched && !gpsContTotalMin;
        loraInitFirstTime();
        /* 读取电量并缓存（每轮上报前刷新一次，上报帧里带的就是它） */
        for (int i = 5; i > 0; i--) {
          char time_str[20];
          formatTimestamp(getCurrentTimestamp(), time_str, sizeof(time_str));
          printf("[WAKE] %s  %d\r\n", time_str, i);
          wdg_feed();
          HAL_Delay(1000);
        }

        /* 需�?GPS：醒后立刻预搜（搜星窗口�?上报点前 5 �?截止），
         * 保证到点无论有无坐标都能准时发帧 */
        if (doPreSearch) {
          if (leadSec < GPS_PRESEARCH_LEAD_SEC)
            printf("[GPS] period<search max: lead=%lus, best effort\r\n",
                   (unsigned long)leadSec);

          gpsPreSearch();

          {
            uint32_t now2  = getCurrentTimestamp();
            uint32_t wake2 = (nextTxTimestamp > 5u) ? (nextTxTimestamp - 5u) : now2;

            if (wake2 > now2 + 5u) {
              /* 距离上报点还�?>5 秒：GPS 已断电，�?STOP 睡到点前 5 秒�?
               * 醒来只需初始�?LoRa + 倒计时，到点直接发预搜结果（最省电�?*/
              printf("[SCHED] pre done, %lus left, sleep 5s before tx\r\n",
                     (unsigned long)(wake2 - now2));
              loraSleep();
              loraInitialized = 0;
              enterStopUntil(wake2);
            }
          }
        }
      } else if (!gpsContTotalMin && !gpsPreSearched) {
        /* 等待期：MCU �?STOP 深度休眠，RTC 闹钟�?wakeTime 唤醒；RAM 内容保持�?
         * 说明�?
         *   - 类型5(upgps) 允许睡到"上报点前 5 �?，醒后现场搜星再发（不受约束�?
         *   - 类型1 允许睡到"上报点前 提前�?，醒后先预搜再到点发（准点）
         *   所�?GPS 待办不再需要空转等满一个周期（省电�?
         * 双保险：确认 LoRa 已深度休眠（OnMaster 末尾已调过，重复调用安全�?*/
        loraSleep();
        enterStopUntil(wakeTime);
      }
      /* 连续定位模式(gpsContTotalMin)保持活跃空转，由 HAL_Delay(100) 节流 */
    }

    /* GPS: 取出环形缓冲里的 NMEA 语句并打�?解析 */
    wdg_feed();   /* main loop spin (continuous-tracking idle) keeps dog happy */
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
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  /* Already running (e.g. first TX after boot): do not disable its PLL. */
  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) return;
  Safety_Begin(STAGE_CLOCK, 3000u);
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
  osc.HSEState = RCC_HSE_ON;
  osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  osc.HSIState = RCC_HSI_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    /* Keep the device recoverable if the external crystal cannot start. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.PLL.PLLState = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Safety_Reset(RESET_INIT);
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Safety_Reset(RESET_INIT);
    clockFallback = 1u;
  } else clockFallback = 0u;
  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Safety_Reset(RESET_INIT);
  if (huart1.Instance == USART1) MX_USART1_UART_Init();
  if (huart3.Instance == USART3) {
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    if (HAL_UART_AbortReceive(&huart3) != HAL_OK) Safety_Reset(RESET_INIT);
    MX_USART3_UART_Init();
  }
  Safety_Begin(STAGE_MAIN, 15000u);
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
  * @brief USART3 Initialization Function (GPS 模块 9600, 仅接�?
  * @note  PB11 = USART3_RX（PB10/TX �?HAL_UART_MspInit 一并配置，但本模式不用�?
  *        中断优先级设�?2，低�?LoRa(EXTI2=0)，避免影响射频时�?
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

  /* 开�?GPS 逐字节中断接�?*/
  HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);

  /* USER CODE END USART3_Init 2 */
}

/**
  * @brief  GPIO 初始化函�?
  * @note   配置说明�?
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

  /* PB15 = GPS 电源开关：推挽输出，默认高电平 = GPS 断电（极�? �?上电）�?
   * 搜星�?gpsPowerOn() 拉低上电，搜�?gpsPowerOff() 拉高断电 */
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin   = GPS_PWR_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPS_PWR_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* GNSS 唤醒�?(PB1 �?模块 WAKE_UP)：搜星状态同步控�?
   *   搜星�?  gpsPowerOn()  拉高 �?Full on（先拉高再上电，见该函数时序说明�?
   *   非搜星时 gpsPowerOff() 拉低 �?Standby(20uA)，RTC 域保持可热启�?
   * 初始为低：开机即"非搜�?状态（此时 PB15 也是断电，低电平无副作用�?
   * 若模块因硬件过渡期常供电，则�?Standby 省电，等第一次搜星拉高唤醒） */
  /* GPS 复位引脚 (PA3 �?L76K RESET_N) - 平时高电�?不复�? */
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
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET); /* �?= Standby */

  /* LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* LoRa CSN 片选引�?(PB0) - 默认高电�?*/
  GPIO_InitStruct.Pin  = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);

  /* LoRa RST 复位引脚 (PA1) - 默认高电�?*/
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin  = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);

  /* LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin  = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
  * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI�?
  * @note   CPOL=0, CPHA=0, MSB, 软件 NSS, 波特�?4.5MHz
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
  * @brief  ADC1 初始化（电池电量检�?PA0/ADC1_CH0，寄存器方式�?
  * @note   不依�?stm32f1xx_hal_adc.c，直接操作寄存器
  *         硬件：电�?�?100K �?PA0 �?100K �?GND�?:1 分压�?
  */
static void MX_ADC1_Init(void)
{
  uint32_t timeout;
  Safety_Begin(STAGE_ADC, 2000u);

  /* 1. 先设�?ADC 时钟分频 /8（必须在 ADC 时钟使能之前�?*/
  RCC->CFGR &= ~(3u << 14);
  RCC->CFGR |=  (3u << 14);               /* ADCPRE = /8, 9MHz */

  /* 2. 使能 GPIOA �?ADC1 时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIOA->CRL &= ~0xFu; /* PA0 analog input: avoid digital input leakage. */
  RCC->APB2ENR |= (1u << 9);              /* ADC1EN = 1 */
  (void)RCC->APB2ENR;

  /* 3. 复位 ADC1 外设 */
  RCC->APB2RSTR |=  (1u << 9);
  RCC->APB2RSTR &= ~(1u << 9);

  /* 4. 配置寄存器：单通道单次转换，CH0(PA0)，最长采样时�?*/
  ADC1->CR1 = 0;
  ADC1->CR2 = 0;
  ADC1->SQR1 = 0;                         /* 1 个转�?*/
  ADC1->SQR2 = 0;
  ADC1->SQR3 = 0;                         /* CH0 (PA0) */
  ADC1->SMPR2 = (7u << 0);                /* CH0 采样 239.5 周期 */

  /* 5. 上电 ADC */
  ADC1->CR2 = ADC_CR2_ADON;
  HAL_Delay(10);

  /* 6. 校准（STM32F1 必须校准才能正常工作�?*/
  ADC1->CR2 |= ADC_CR2_RSTCAL;
  timeout = 1000000;
  while ((ADC1->CR2 & ADC_CR2_RSTCAL) && --timeout);
  if (!timeout) Safety_Reset(RESET_ADC);
  ADC1->CR2 |= ADC_CR2_CAL;
  timeout = 1000000;
  while ((ADC1->CR2 & ADC_CR2_CAL) && --timeout);
  if (!timeout) Safety_Reset(RESET_ADC);



  /* 7. 空读一次：确认 ADON 触发 + EOC 等待这条链路通，结果丢弃 */
  (void)ADC1_Read();
  ADC1->CR2 &= ~ADC_CR2_ADON;
  Safety_Begin(STAGE_MAIN, 15000u);
}

/**
  * @brief  NVIC 中断控制器初始化（LoRa IRQ EXTI2�?
  */
static void MX_NVIC_Init(void)
{
  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

/* USER CODE BEGIN 4 */

/**
  * @brief  ADC1 单次读取（寄存器方式�?
  * @return 12 �?ADC 值（0~4095�?
  */
static uint16_t ADC1_Read(void)
{
  uint32_t timeout = 1000000;
  /* STM32F1: 设置 ADON 触发转换（比 SWSTART 更可靠） */
  ADC1->CR2 |= ADC_CR2_ADON;
  while (!(ADC1->SR & ADC_SR_EOC) && --timeout);
  if (!timeout) Safety_Reset(RESET_ADC);
  return (uint16_t)(ADC1->DR & 0xFFF);
}

/**
  * @brief  获取电池电量百分比（0~99�?
  * @note   PA0 接分压电路（100K+100K），V_bat = ADC�?4096 * 3.3V * 2
  *         锂电池映射：4.2V=99%, 3.3V=0%，分段线性插�?
  */
static uint8_t GetBatteryPercent(void)
{
  uint32_t adc_sum = 0;
  Safety_Begin(STAGE_ADC, 2000u);
  ADC1->CR2 |= ADC_CR2_ADON;
  HAL_Delay(1u);
  uint8_t i;

  /* 连续采样 8 次取平均，抑制噪�?*/
  for (i = 0; i < 8; i++) {
    adc_sum += ADC1_Read();
  }
  ADC1->CR2 &= ~ADC_CR2_ADON;
  Safety_Begin(STAGE_MAIN, 15000u);
  uint16_t adc_val = adc_sum / 8;

  /* 计算电池电压：分压比 1:1，基�?3.3V */
  /* V_bat(mV) = adc_val * 3300 * 2 / 4096 = adc_val * 6600 / 4096 */
  uint32_t bat_mv = (uint32_t)adc_val * 6600 / 4096;

  /* 锂电池电�?�?百分�?分段线性映�?*/
  uint8_t percent;
  if (bat_mv >= 4200)      percent = 99;
  else if (bat_mv >= 4000) percent = 80 + (bat_mv - 4000) * 19 / 200;
  else if (bat_mv >= 3700) percent = 50 + (bat_mv - 3700) * 30 / 300;
  else if (bat_mv >= 3500) percent = 20 + (bat_mv - 3500) * 30 / 200;
  else if (bat_mv >= 3300) percent = (bat_mv - 3300) * 20 / 200;
  else                     percent = 0;

  if (percent > 99) percent = 99;

  printf("[BAT] ADC=%d V=%lu.%01luV => %d%%\r\n",
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
 * @note   只做两件最轻的动作：入环形缓冲 + 重新开启下一字节接收�?
 *         绝不�?ISR 里做字符串解析或 printf —�?那样会长时间占用中断�?
 *         缓冲满时丢弃本次字节，下一条语句的 '\n' 会自动重新同步�?
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
 * @brief  UART 错误回调（GPS 串口出错自愈�?
 * @note   GPS 上电瞬间的波特率毛刺/EMI 会触�?ORE/NE/FE 错误，HAL 会中�?
 *         接收；不重新武装的话 GPS 此后永远无数据（表现为搜星必失败）�?
 *         这里清标志并重启逐字节接收�?
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1);
  }
}

/**
 * @brief  GPS 上电：PB15 置低 �?模块开机开始搜�?
 * @note   1) 电源是冷启动（不使用 WAKE_UP 休眠模式），上电到输�?NMEA
 *            需�?GPS_PWR_ON_DELAY_MS（断电重启比 Standby 唤醒更久�?
 *         2) 清掉环形缓冲里可能残留的半截语句，避免解析出�?
 *         3) 每次上电模块时钟域都是冷的，首次定位时间比热启动长，属正�?
 */
static void gpsPowerOn(void)
{
  if (!gpsBatteryOk()) return;
  gpsRd = gpsWr;      /* 丢弃残留字节（关中断非必需：ISR 只改 gpsWr�?*/
  gpsLineLen = 0;
  gpsRxCount = 0;     /* 本轮重新统计串口数据量，便于观察模块有没有在吐数�?*/

  /* 顺序关键：先�?WAKE_UP(PB1) 拉高，再上电 —�?
   * 若上电瞬�?WAKE_UP 为低，模块会直接�?Standby 而不搜星�?
   * 两种起始状态都覆盖�?
   *   - 模块此前被断�?PB15=�?：上电时读到 WAKE_UP=�?�?直接 Full on�?
   *   - 模块此前�?Standby(PB1=低保�?：拉高即完成唤醒（热启动 ~2 秒） */
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_SET); /* �?= Full on */
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_RESET);   /* �?= 上电 */
  HAL_Delay(GPS_PWR_ON_DELAY_MS);
  gpsPowerState = 1;
  printf("[GPS] Power ON (PB15 low, PB1 Full on)\r\n");
}

/**
 * @brief  GPS 断电：PB15 置高 �?模块完全关机
 * @note   断电后旧坐标立即失效：下一轮不能把上次的定位当成有效坐标上报，
 *         否则 GPS_FIX_TTL_MS 内会误判�?已定�?而跳过真正的新一轮搜�?
 */
static void gpsPowerOff(void)
{
  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);     /* �?= 断电 */
  /* WAKE_UP(PB1) 拉低 �?模块�?Standby(20uA)�?
   *   - 若模块已断电：低电平被保持，下次上电�?PowerOn 会先拉高�?
   *   - 若模块仍供电（硬件过渡期常供电）：进�?Standby 低功耗待机，
   *     RTC 域保持，下次拉高即热启动(~2 �?，比冷启动省大量搜星电流 */
  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET); /* �?= Standby */

  gpsFixValid = 0;
  gpsPowerState = 0;   /* 断电后串口数据一律在 gpsTask 里丢�?*/
  gpsRd = gpsWr;
  gpsLineLen = 0;

  printf("[GPS] off\r\n");
}

/* USER CODE END 4 */

/**
  * @brief  错误处理函数
  */
void Error_Handler(void)
{
  /* Works before UART and SysTick exist, and from interrupt context. */
  Safety_Reset(RESET_INIT);
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
