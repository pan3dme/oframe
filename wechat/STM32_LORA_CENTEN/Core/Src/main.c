/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : PAN3029 LoRa 收发器主程序 - STM32F103C8T6
 *                   与 CenterReceiveBLE_v4 LoRa 配置匹配
 * @note           功能说明：
 *                 1. LoRa 通信：支持接收和发送两种模式，可通过按键 K1/K2 切换
 *                 2. LED 指示：LED1 接收状态指示（收到数据亮500ms）
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
// STM32 HAL 库主头文件，包含所有 HAL 驱动接口
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "common_test.h"  // 公共测试模块（Common 目录共享）
#include "pan3029_port.h" // PAN3029 硬件端口适配层头文件
#include "pan3029_rf.h"   // PAN3029 LoRa 射频驱动头文件
#include "radio.h"        // LoRa 射频参数配置 (DEFAULT_FREQ/DEFAULT_SF 等)
#include "stdio.h"        // 标准输入输出库，用于 printf 调试输出
#include "stdlib.h"       // 标准库函数，提供 atoi() 等工具函数
#include "string.h"       // 字符串处理库，提供 memcpy/strncmp/memset 等函数

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* DTU 发送队列：环形缓冲区，每条消息最大 256 字节，最多 5 条 */
#define DTU_QUEUE_SIZE 5
#define DTU_MSG_MAX_LEN 256

typedef struct {
  char buf[DTU_MSG_MAX_LEN]; // 消息内容
  uint16_t len;              // 消息长度
} DtuQueueItem_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EXAMPLE_NAME "LY-020 LoRa Gps" // 工程名称
#define DEMO_VER "V1.0"                // 演示版本号
#define EXAMPLE_DATE "2026/10/04"      // 工程日期

#define RX_LEN 36             // LoRa 接收缓冲区最大长度（字节）
#define LORA_TX_DELAY_MS 1500 // LoRa 接收后延时发送时间（ms）

/* Work mode - 工作模式定义 */
#define MODE_IDLE 0 // 空闲模式
#define MODE_RX 1   // 接收模式
/* MODE_TX 已删除：进入该模式的唯一途径是按键 K2，K2 移除后无触发源，
 * 连同 do_tx_send() / txMsg / lastTxTick 一并移除。
 * 恢复定时发射时把 #define MODE_TX 2 补回即可 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* hcan(CAN) / hi2c1(I2C) 已删除：
 * 两者的 MX_xxx_Init() 在整个工程里从未定义也从未调用，
 * 即 CAN / I2C 外设从未被初始化过，属于死代码，
 * 连同 MSP 初始化函数、CAN 的两个 ISR、HAL 驱动与 .ioc 引脚一并移除 */
SPI_HandleTypeDef hspi1; // SPI1 用于 LoRa PAN3029（硬件 SPI）
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3; // USART3 用于 DTU 通信

static char deviceName[16] = "vv-xx"; // 设备名称（根据 UID 动态生成）
 
  // {0x67151723, 0x48825550, 0x0669FF49}, // 这是插板编号
//   0 -> Chip UID    : 67151723-48825550-0669FF49
/* USER CODE BEGIN PV */
/* 设备 UID 列表（10 台设备），用于根据芯片 UID 确定设备编号 */
static const uint32_t deviceUidList[10][3] = {
    {0x003A002E, 0x46343030, 0x35343138}, // 设备 0
    {0x003A002E, 0x46343030, 0x35343139}, // 设备 1
    {0x003A002E, 0x46343030, 0x3534313A}, // 设备 2
    {0x003A002E, 0x46343030, 0x3534313B}, // 设备 3
    {0x003A002E, 0x46343030, 0x3534313C}, // 设备 4
    {0x003A002E, 0x46343030, 0x3534313D}, // 设备 5
    {0x003A002E, 0x46343030, 0x3534313E}, // 设备 6
    {0x67151723, 0x48825550, 0x0669FF49}, // 这是插板编号
    {0x67184122, 0x49867050, 0x066BFF49}, // 设备 8
    {0x67184240, 0x49867050, 0x0669FF49}, // 设备 9

};

// PB15 (LED1) 接收指示：收到LoRa数据时亮500ms
static uint32_t led1OnTick = 0; // LED1 点亮时间戳
static uint8_t led1On = 0;      // LED1 当前是否点亮（0=灭, 1=亮）

// 工作模式相关

static uint8_t workMode = MODE_RX;     // 当前工作模式（RX/TX/IDLE）
                                       // （txMsg/lastTxTick 已随 do_tx_send 移除，
                                       //   原按键消抖时间戳已随 K1/K2 移除）

/* LoRa receive buffers - LoRa 接收缓冲区 */
static uint8_t prev_rx_buf[RX_LEN] = {
    0};                          // 上次接收到的数据（用于对比是否重复）
static uint8_t prev_rx_size = 0; // 上次接收数据的长度

extern struct RxDoneMsg
    RxDoneParams; // 外部声明：接收完成时的消息结构体（在驱动中定义）

/* DTU (USART3) 接收缓冲区 */
#define DTU_RX_BUF_SIZE 256
static uint8_t dtuRxBuf[DTU_RX_BUF_SIZE]; // DTU 接收缓冲区
static uint16_t dtuRxLen = 0;             // 当前接收到的数据长度
static uint32_t dtuLastRxTick = 0;   // 上次收到字节的时间戳（用于超时判断）
static uint32_t dtuCsqTick = 0;      // nettime 查询定时器：记录上次发送时间
static char dtuNetTimeStr[32] = "0"; // DTU 网络时间戳（秒），用于上报
static uint32_t dtuTimestamp = 0;    // DTU 网络时间 Unix 时间戳（秒）
static uint32_t dtuSyncTick = 0;     // 对时时的 HAL_GetTick() 值，用于实时计算
static uint32_t lastPrintTick = 0;   // [调试] 每秒打印时间用
static uint8_t dtuTimeValid = 0;     // 网络时间是否已获取成功
static int dtuCsqRssi = 0;           // DTU CSQ 信号强度值
static uint8_t dtuNetTimeFailCount = 0;  // DTU 网络时间连续失败计数器（达到 3 次复位 DTU）
static uint32_t rtcSendCount = 0;    // 对时上报计数器
static int batteryValue = 85;        // 电池电量（%）
static char configStr[32] = "30,0M"; // 配置信息字符串（可通过DTU修改）
static char centerStr[32] = "";      // 农场中心坐标 "lat,lng"（空=未设置）
static uint8_t configBootSent = 0;   // 开机配置是否已上报（0=未上报, 1=已上报）

/* DTU 发送队列变量 */
static DtuQueueItem_t dtuQueue[DTU_QUEUE_SIZE];
static uint8_t dtuQueueHead = 0;     // 出队索引
static uint8_t dtuQueueTail = 0;     // 入队索引
static uint8_t dtuQueueCount = 0;    // 当前队列中的消息数量
static uint32_t dtuLastSendTick = 0; // 上次发送时间戳（用于 100ms 间隔控制）

/* Flash 配置存储
 * 同一页内分区存放（Flash 擦除以页为单位，任何一项写入都要整页重写）：
 *   +0  : CONFIG_MAGIC(4B) + 对时配置字符串(32B)
 *   +64 : CENTER_MAGIC(4B) + 农场中心坐标字符串(32B) */
#define CONFIG_FLASH_ADDR 0x0800FC00 // Flash 最后一页（STM32F103C8T6 64KB）
#define CONFIG_MAGIC 0xA5A5A5A5      // 有效数据标志
#define CENTER_FLASH_OFF 64          // 农场中心坐标区在配置页内的偏移
#define CENTER_MAGIC 0x5A5A5A5A      // 中心坐标有效数据标志

/* 工作时间 62 进制字典
 * 配置串格式: "<周期分钟>,<工作时间2字符>"，如 "30,0M"
 * @note 顺序必须严格为: 数字 + 大写字母 + 小写字母（共 62 个）
 *       已用实配值核对: "00"=00:00~01:00, "0G"=00:00~17:00, "38"=10:00~20:00
 *       其中 "0G" 是唯一能区分大小写块顺序的判例，一旦对调结果就变成 01:00~21:00
 * @warning 顺序若与云端不一致，解码值会整体偏移，务必保持三块次序不变 */
#define TIME_DICT "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

/* 待执行指令缓存：每个设备最多缓存一条 DTU 下发的指令 */
#define PENDING_CMD_MAX_SLOTS 8 // 最多缓存 8 个设备的待执行指令

typedef struct {
  uint8_t used;      // 该槽位是否被占用
  char deviceId[32]; // 目标设备 ID
  char cmd[32];      // 指令内容
  char val[32];      // 指令参数
} PendingCmd_t;

static PendingCmd_t pendingCmdCache[PENDING_CMD_MAX_SLOTS];

/* gpstm 中继指令状态：收到 gpstm 后在有效期内下发 upgps 指令 */
static uint8_t gpstmActive = 0;      // gpstm 中继是否激活（0=未激活, 1=激活）
static uint32_t gpstmRecvTick = 0;   // 收到 gpstm 时的 HAL_GetTick() 值
static uint32_t gpstmTimeoutMin = 0; // gpstm 有效时长（分钟），来自 value 字段

/* lorasw 指令状态：LoRa 射频开关
 *   1 = 射频工作，正常收发
 *   0 = 射频深度休眠（rf_deepsleep），不再收发
 * @note 休眠只关 LoRa 芯片，MCU 与 4G/DTU 继续运行，
 *       所以云端仍可经 4G 下发 lorasw|1 远程唤醒
 * @warning 该状态只存 RAM，刻意不落盘：默认和重启后都是"可收发"，
 *          掉电或复位即自动恢复收发，不会出现"关掉后醒不过来"的死状态
 *          若要让 lorasw|0 跨重启保持，必须落盘，但那会引入上述风险 */
static uint8_t loraSwOn = 1;         // LoRa 射频开关（1=开, 0=关）

/* 工作区间自动控制状态
 * @note 边沿触发：只在"进出工作区间"的那一刻自动开关射频，
 *       区间内部不强制，这样区间内手动 lorasw|0 关掉后会一直保持关闭，
 *       直到区间结束再进入才自动重开 —— 手动关在区间内才有效
 * @warning 工作区间优先级高于 lorasw 指令：
 *          区间外一律休眠，且拒绝 lorasw|1 开启 */
static uint8_t prevInWorkWindow = 1;   // 上次是否在工作区间内（边沿检测）
static uint32_t workWinCheckTick = 0;  // 工作区间检测节流时间戳

/* 按键 K1/K2 相关代码已整体移除：本板未焊接 K1/K2，只保留复位键
 * 移除原因见 MX_GPIO_Init 中 K1/K2 引脚配置处的说明（悬空会伪造中断） */

/* USER CODE END PV */
/* Private function prototypes -----------------------------------------------*/
// 系统函数声明
void SystemClock_Config(void);         // 系统时钟配置函数
static void MX_GPIO_Init(void);        // GPIO 初始化函数
static void MX_SPI1_Init(void);        // SPI1 初始化（LoRa 硬件 SPI）
static void MX_USART1_UART_Init(void); // USART1 初始化（调试串口）
static void MX_USART3_UART_Init(void); // USART3 初始化（DTU 通信）
static void MX_NVIC_Init(void);        // NVIC 中断控制器初始化

/* USER CODE BEGIN PFP */
// 用户自定义函数声明
static void printf_logo(void);   // 打印工程信息 Logo
static int getDeviceIndex(void); // 根据芯片 UID 获取设备编号
static void OnSlave(void);       // LoRa 从机接收处理函数
static void enter_rx_mode(void);   // 切换到接收模式
static void lora_rf_sleep(void);   // 关闭 LoRa 射频（深度休眠）
static uint8_t lora_rf_wake(void); // 唤醒 LoRa 射频并恢复接收
/* do_tx_send() 已随 MODE_TX 一并删除（无触发源） */
static void sendToDtu(const char *loraStr, int rssi,
                      int snr); // 统一上报DTU（JSON格式，入队）
static void enqueueDtuMsg(const char *msg, int len); // 将消息加入 DTU 发送队列
static void processDtuQueue(void);     // 处理 DTU 发送队列（每秒发一条）
static void loadConfigFromFlash(void); // 从 Flash 加载配置 + 农场中心坐标
static void flash_save_all(void);   // 整页重写：对时配置 + 农场中心坐标
static void sendCenterToDtu(void);  // 上报农场中心坐标 1|name|lat,lng|
static int center_coord_valid(double lat, double lng); // 经纬度合法性校验
static void parseDtuCommand(char *buf);         // JSON 解析 DTU 下发指令
static uint32_t getCurrentTimestamp(void);      // 获取当前实时时间戳
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour,
                                    int min, int sec); // 日期转Unix时间戳
static uint32_t getConfigIntervalMs(void); // 从 configStr 解析对时间隔（毫秒）
static int twoCharToIndex(const char *str); // 2 字符按 TIME_DICT 解成 62 进制值
static int getWorkTimeFromConfig(void);    // 取配置里周期后的工作时间字段并解码
static uint8_t indexToTimeWindow(int idx, uint8_t *outStart,
                                uint8_t *outEnd); // idx 转工作时间窗口
static uint8_t workWindowEndAdj(uint8_t end); // 工作窗口结束小时修正（方案C）
static uint8_t getWorkClockHour(void);        // 工作时钟：取当天小时(0~23)
static uint8_t isInWorkWindow(void);          // 当前是否处于工作区间内
static void printWorkTimeDebug(void);      // 打印工作时间解码过程（调试用）
static void cachePendingCommand(const char *deviceId, const char *cmd,
                                const char *val); // 缓存待执行指令
static uint8_t
checkPendingCommand(const char *deviceId); // 检查是否有缓存指令，返回1=有,0=无
static uint8_t sendPendingCommand(
    const char *deviceId); // 发送缓存指令并清除，返回1=已发送,0=无

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  根据芯片 UID 获取设备编号
 * @note   读取芯片 96 位 UID，在 deviceUidList 中查找匹配项
 * @retval 设备编号 (0-9)，未找到返回 -1
 */
static int getDeviceIndex(void) {
  uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7E8;
  uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7EC;
  uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7F0;

  for (int i = 0; i < 10; i++) {
    if (deviceUidList[i][0] == uid2 && deviceUidList[i][1] == uid1 &&
        deviceUidList[i][2] == uid0) {
      return i;
    }
  }
  return -1; // 未找到匹配的设备
}

#ifdef __GNUC__
// GCC 编译器使用 __io_putchar 实现 printf 重定向
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
// Keil ARMCC 编译器使用 fputc 实现 printf 重定向
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
 * @brief  printf 底层输出函数重定向
 * @note   将 printf 输出的每个字符通过 USART1 发送出去
 * @param  ch: 要输出的字符
 * @retval 返回输出的字符
 */
PUTCHAR_PROTOTYPE {
  // 通过 USART1 发送单个字符，超时时间 0xFFFF
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

/**
 * @brief  打印工程信息 Logo
 * @note   在系统启动时调用，显示工程名称、版本、日期、硬件配置等信息
 */
static void printf_logo(void) {
  printf("\n\r");
  printf("*************************************************************\r\n");
  printf("* Project Name  : %s\r\n", EXAMPLE_NAME); // 工程名称
  printf("* Demo Version  : %s\r\n", DEMO_VER);     // 版本号
  printf("* Date          : %s\r\n", EXAMPLE_DATE); // 日期
  printf("* MCU           : STM32F103C8T6\r\n");    // 主控芯片
  printf("* Radio         : PAN3029 (LoRa)\r\n");   // 射频模块
  printf("*************************************************************\r\n");
}

/**
 * @brief  LoRa 从机接收处理函数
 * @note   功能说明：
 *         1. 检查接收完成标志，若收到数据则读取 RSSI/SNR/数据内容
 *         2. 与上次接收的数据对比，标记为 [SAME] 或 [DIFF]
 *         3. 打印接收信息（信号强度、信噪比、长度、内容）
 *         4. 处理接收错误（CRC 错误、超时）
 *         5. 重新进入单次超时接收模式（超时时间 5-15 秒）
 */
static void OnSlave(void) {
  /* LoRa 射频已被 lorasw 关闭：不处理任何射频事件，
   * 更不能重新进入接收（否则会一直把睡下去的射频唤醒，白耗电） */
  if (!loraSwOn) {
    return;
  }

  // 检查是否接收完成
  if (rf_get_recv_flag() == RADIO_FLAG_RXDONE) {
    uint8_t local_buf[RX_LEN]; // 本地接收缓冲区
    uint8_t local_size;        // 接收数据长度
    uint8_t local_rssi;        // 接收信号强度指示 (寄存器原始值，需减256得dBm)
    float local_snr;           // 信噪比 (dB)
    uint8_t is_same = 0;       // 数据重复标志

    // 临界区保护：关中断，快速读取接收数据
    __disable_irq();
    rf_set_recv_flag(RADIO_FLAG_IDLE); // 清除接收完成标志
    local_rssi = RxDoneParams.Rssi;    // 读取 RSSI
    local_snr = RxDoneParams.Snr;      // 读取 SNR
    local_size = RxDoneParams.Size;    // 读取数据长度
    if (local_size > RX_LEN)
      local_size = RX_LEN; // 防止缓冲区溢出
    for (uint8_t i = 0; i < local_size; i++)
      local_buf[i] = RxDoneParams.Payload[i]; // 拷贝数据到本地缓冲区
    __enable_irq();                           // 开中断

    /* 与上次接收的数据对比，判断是否重复 */
    if (local_size == prev_rx_size && prev_rx_size > 0) {
      is_same = 1; // 假设相同
      for (uint8_t i = 0; i < local_size; i++) {
        if (local_buf[i] != prev_rx_buf[i]) {
          is_same = 0; // 发现不同，标记为不同
          break;
        }
      }
    }

    // 打印接收信息
    printf("Rssi: %d  ",
           local_rssi - 256);          // RSSI 值（需减去 256 得到实际 dBm）
    printf("Snr: %.1f   ", local_snr); // SNR 值（dB）
    printf("Len: %d  ", local_size);   // 数据长度
    printf("Str: ");
    for (uint8_t i = 0; i < local_size; i++) {
      if (local_buf[i] == 0)
        break;                    // 遇到字符串结束符停止
      printf("%c", local_buf[i]); // 打印字符
    }
    if (is_same)
      printf("  [SAME]"); // 标记为重复数据
    else
      printf("  [DIFF]"); // 标记为新数据
    printf("\r\n");

    /* 保存当前数据作为上次数据，用于下次对比 */
    prev_rx_size = local_size;
    for (uint8_t i = 0; i < local_size; i++)
      prev_rx_buf[i] = local_buf[i];

    /* PB15 (LED1) 点亮，指示收到LoRa数据，500ms后由主循环熄灭 */
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    led1OnTick = HAL_GetTick();
    led1On = 1;

    /* 解析消息类型并处理上报 */
    if (local_size < RX_LEN)
      local_buf[local_size] = '\0';
    int msgType = atoi((char *)local_buf);
    printf("[LoRa] MsgType: %d\r\n", msgType);

    if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_UP_GPS|| msgType == MSG_TYPE_TIME) {
      /* 从 LoRa 数据中提取 deviceId（第二个 |
       * 分隔字段），检查是否有缓存的待执行指令 */
      uint8_t hasPendingCmd = 0;
      char loraDevId[32] = {0};
      const char *p1 = strchr((char *)local_buf, '|');
      if (p1) {
        const char *p2 = p1 + 1;
        const char *p3 = strchr(p2, '|');
        int idLen = p3 ? (int)(p3 - p2) : (int)strlen(p2);
        if (idLen > 31)
          idLen = 31;
        memcpy(loraDevId, p2, idLen);
        loraDevId[idLen] = '\0';
        hasPendingCmd = checkPendingCommand(loraDevId);
      }

      char reportBuf[RX_LEN];

      if (msgType == MSG_TYPE_TIME) {
        // 调用 common 函数替换时间字段（仅用于上报 DTU）
        int ret = common_replaceTimeSegment((char *)local_buf, reportBuf,
                                            sizeof(reportBuf),
                                            getCurrentTimestamp(), NULL);
        if (ret == 0) {
          printf("[LoRa 接收] : %s\r\n", reportBuf);
          sendToDtu(reportBuf, local_rssi - 256, (int)local_snr);
        } else {
          sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
        }
      } else {
        // MSG_TYPE_GPS: 直接上报原始数据
        sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
      }
		  if (msgType == MSG_TYPE_GPS || msgType == MSG_TYPE_TIME) {

				/* 统一延时后下发：有缓存指令则下发指令，否则检查 gpstm 中继或对时 */
				HAL_Delay(LORA_TX_DELAY_MS);

				if (hasPendingCmd) {
					sendPendingCommand(loraDevId);
				} else if (gpstmActive && (HAL_GetTick() - gpstmRecvTick) <
																			gpstmTimeoutMin * 60 * 1000) {
					/* gpstm 中继有效：在有效期内，任意设备上报时下发 upgps 指令 */
					char txData[64];
					int txLen = snprintf(txData, sizeof(txData), "%d|%s|upgps|0",
															 MSG_TYPE_COM, loraDevId);
					uint32_t tx_time = 0;
					rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
					printf("[LoRa gpstm 中继] %s\r\n", txData);

					/* 等待发送完成 */
					uint32_t waitStart = HAL_GetTick();
					while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE) {
						if (HAL_GetTick() - waitStart > 3000)
							break;
					}
					rf_set_transmit_flag(RADIO_FLAG_IDLE);
				} else {
					// 构造下发数据: 4|deviceName|timestamp| （使用 STM32 实时时间戳）
					char txData[64];
					int txLen =
							snprintf(txData, sizeof(txData), "%d|%lu|%s", MSG_TYPE_SYN_TIME,
											 (unsigned long)getCurrentTimestamp(), deviceName);

					// 发送 LoRa 数据
					uint32_t tx_time = 0;
					rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
					printf("[LoRa 下发对时] %s\r\n", txData);

					// 等待发送完成
					uint32_t waitStart = HAL_GetTick();
					while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE) {
						if (HAL_GetTick() - waitStart > 3000)
							break; // 3秒超时保护
					}
					rf_set_transmit_flag(RADIO_FLAG_IDLE);
				}
			}
		}

    /* MSG_TYPE_CONFIG: 直接上报原始数据，不对时 */
    if (msgType == MSG_TYPE_CONFIG) {
      sendToDtu((char *)local_buf, local_rssi - 256, (int)local_snr);
    }

    rf_enter_single_timeout_rx(15000); // 重新进入接收模式，超时 15 秒
  }

  // 处理 CRC 校验错误
  if (rf_get_recv_flag() == RADIO_FLAG_RXERR) {
    printf("crc error\r\n");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000); // 重新进入接收模式，超时 5 秒
  }

  // 处理接收超时
  if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT) {
    printf(".");
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    rf_enter_single_timeout_rx(5000); // 重新进入接收模式，超时 5 秒
  }
}

/**
 * @brief  切换到接收模式
 * @note   功能说明：
 *         1. 设置工作模式为 MODE_RX
 *         2. 关闭 LED1（接收模式下 LED1 以 500ms 间隔闪烁）
 *         3. 打印模式切换信息
 *         4. 进入单次超时接收模式，超时时间 5 秒
 */
static void enter_rx_mode(void) {
  workMode = MODE_RX; // 设置工作模式为接收
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET); // 关闭 LED1
  led1On = 0; // 同步 LED1 状态变量
  printf("\r\n>> Switch to RX mode\r\n");
  rf_enter_single_timeout_rx(5000); // 进入接收模式，超时 5 秒
}

/* =====================================================================
 * @brief  关闭 LoRa 射频，进入深度休眠
 * @note   只关 LoRa 芯片，MCU 与 4G/DTU 继续运行，
 *         因此云端仍可经 4G 下发 lorasw|1 远程唤醒
 * @warning 4G/DTU 必须始终保持工作，不得在此加入任何 4G 休眠/断电动作！
 *          否则中继一旦被 lorasw|0 关掉，就再也收不到唤醒指令，只能现场断电
 *          4G 的周期任务（CSQ/nettime 查询、processDtuQueue、parseDtuCommand）
 *          都跑在主循环里，不在被门禁挡住的 OnSlave() 内，休眠期间照常运行
 * ===================================================================== */
static void lora_rf_sleep(void) {
  rf_deepsleep();
  loraSwOn = 0;
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET); // 关闭 LED1
  led1On = 0;
  printf("[LoRa开关] 射频已关闭(深度休眠), MCU与4G仍工作\r\n");
}

/* =====================================================================
 * @brief  唤醒 LoRa 射频，恢复接收
 * @note   PAN3029 深度休眠后寄存器掉电，必须重新 rf_init + 恢复参数，
 *         不能直接 rf_enter_single_timeout_rx
 * @retval 1=唤醒成功, 0=唤醒失败
 * ===================================================================== */
static uint8_t lora_rf_wake(void) {
  int retry = 0;

  /* 与开机初始化一致：先确保 RST 为高，失败则复位后重试一次 */
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  while (rf_init() != RF_OK) {
    if (retry >= 2) {
      printf("[LoRa开关] 射频唤醒失败, 请检查接线\r\n");
      return 0;
    }
    retry++;
    printf("[LoRa开关] 初始化失败, RST复位后重试(%d)\r\n", retry);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
  }

  rf_set_default_para();          // 恢复频率/SF/BW/功率等参数
  rf_enter_single_timeout_rx(5000); // 立即进入接收
  loraSwOn = 1;
  printf("[LoRa开关] 射频已开启, 恢复接收\r\n");
  return 1;
}

/* do_tx_send()（每 3 秒发 "I LOVE YOU" 的测试发射）已删除
 * 原因：它只由 MODE_TX 触发，而进入 MODE_TX 的唯一途径是按键 K2，
 *       K2 移除后该模式再无任何入口，整个函数与 txMsg / lastTxTick /
 *       MODE_TX 均成死代码，一并移除
 * 恢复方法：补回本函数与 txMsg / lastTxTick，在主循环 switch 里补
 *       case MODE_TX，并另给一个切换到 MODE_TX 的触发源 */

/**
 * @brief  将日期时间转换为 Unix 时间戳（秒）
 * @note   基于 1970-01-01 00:00:00 UTC，不考虑时区
 */
static uint32_t dateTimeToTimestamp(int year, int month, int day, int hour,
                                    int min, int sec) {
  static const int daysBeforeMonth[] = {0,   0,   31,  59,  90,  120, 151,
                                        181, 212, 243, 273, 304, 334};
  uint32_t days, timestamp;
  int y;

  // 计算从 1970-01-01 到指定日期的天数
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

/**
 * @brief  从 configStr 解析对时间隔（毫秒）
 * @note   configStr 格式如 "30,0M"，取逗号前的数字作为分钟数
 *         限制范围：5~60 分钟，超出则按边界值处理
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
 * @brief  把 2 个字符按 TIME_DICT 解成 62 进制数值
 * @note   与云端 twoCharToIndex() 算法一致：高位 * 62 + 低位
 * @param  str: 恰好 2 个字符的字符串（长度不是 2 直接返回 -1）
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
 * @brief  把 idx 换算成一天里的工作时间窗口（小时级，最小间隔 1 小时）
 * @note   枚举规则：起始小时 s = 0~22，每个 s 对应 (23-s) 个窗口，
 *         总计 23+22+...+1 = 276 个，故 idx 合法范围仅 0~275
 *         映射: outStart = s, outEnd = s + 1 + (idx - 该 s 之前的累计)
 * @warning 不支持跨零点（如 22:00~06:00），因为算法保证 outEnd > outStart
 * @param  idx:      0~275
 * @param  outStart: 传出起始小时 0~22
 * @param  outEnd:   传出结束小时 1~23（区间为 [start, end) ）
 * @retval 1=换算成功, 0=idx 非法（<0 或 >275）
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
 * @brief  工作窗口结束小时的修正（方案 C）
 * @note   窗口统一按左闭右开 [start, end) 理解，
 *         但编码能表达的最大 end 只有 23，那样 [0,23] 只覆盖到 22:59。
 *         故特例：end==23 时按 24 算，使 [0,23] 表示整天 00:00~23:59
 *         其余窗口不受影响，[0,17] 仍是 00:00~17:00
 * @warning 显示与"将来判断是否工作"必须共用这一个函数，
 *          否则串口看到的和实际行为会对不上
 * ===================================================================== */
static uint8_t workWindowEndAdj(uint8_t end) {
  return (end == 23) ? 24u : end;
}

/* =====================================================================
 * @brief  工作时钟：取当天小时（0~23）
 * @note   与日期无关 —— timestamp % 86400 / 3600，1970 / 2026 都能算
 * @warning 刻意不校验 dtuTimeValid：
 *          未对上时 dtuTimestamp 与 dtuSyncTick 初值均为 0，
 *          本式退化为"开机经过的秒数"，即以 1970-01-01 00:00 为起点递增；
 *          对上 4G 后自动切到真实时间
 *          单独写一份是为了不污染 getCurrentTimestamp()（它仍返回 0，
 *          避免未对上时把假 1970 时间戳上报给云端）
 * ===================================================================== */
static uint8_t getWorkClockHour(void) {
  uint32_t ts = dtuTimestamp + (HAL_GetTick() - dtuSyncTick) / 1000;
  return (uint8_t)((ts % 86400UL) / 3600UL);
}

/* =====================================================================
 * @brief  当前是否处于工作区间内
 * @note   以下三种情况一律"视为在区间内"（射频常开），
 *         目的是任何异常都不能让中继睡死（宁可白耗电也不能丢数据）：
 *           ① 4G 尚未对时 —— 没有可信时间，按 1970 基准判断会误睡
 *           ② 配置里解析不出工作时间字段
 *           ③ idx 越界（>275，无对应窗口）
 * @retval 1=在区间内(可工作), 0=在区间外(应休眠)
 * ===================================================================== */
static uint8_t isInWorkWindow(void) {
  uint8_t ws = 0;
  uint8_t we = 0;
  uint8_t hour;
  uint8_t end;
  int wt;

  if (!dtuTimeValid)
    return 1; /* ① 未对时：不做休眠判断，保持常开 */

  wt = getWorkTimeFromConfig();
  if (wt < 0)
    return 1; /* ② 配置无工作时间字段 */

  if (!indexToTimeWindow(wt, &ws, &we))
    return 1; /* ③ idx 越界 */

  hour = getWorkClockHour();
  end = workWindowEndAdj(we);

  return (hour >= ws && hour < end) ? 1 : 0;
}

/* =====================================================================
 * @brief  取 configStr 里"周期后面的那个参数"并解码成工作时间
 * @note   configStr 格式 "<周期分钟>,<工作时间2字符>"，如 "30,0M"
 *         取**第一个逗号之后**的两个字符
 * @retval >=0 = 工作时间(原始值), -1 = 配置里没有该字段或解码失败
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
 * @brief  打印工作时间的完整解码过程（供核对，暂不参与任何逻辑）
 * @note   把每个字符在字典里的位置也打出来，方便一眼看出字典顺序对不对
 * ===================================================================== */
static void printWorkTimeDebug(void) {
  const char *comma = strchr(configStr, ',');
  const char *p0 = NULL;
  const char *p1 = NULL;
  int wt = getWorkTimeFromConfig();
  char f[3];

  if (comma != NULL && comma[1] != '\0' && comma[2] != '\0') {
    f[0] = comma[1];
    f[1] = comma[2];
    f[2] = '\0';
    p0 = strchr(TIME_DICT, f[0]);
    p1 = strchr(TIME_DICT, f[1]);
  } else {
    f[0] = '?';
    f[1] = '?';
    f[2] = '\0';
  }

  printf("[工作时间] config=%s, 字段=%s, 周期=%lu 分钟\r\n", configStr, f,
         (unsigned long)(getConfigIntervalMs() / 60000));

  if (p0 == NULL || p1 == NULL) {
    printf("[工作时间] 解析失败: 字符不在字典内\r\n");
    return;
  }

  printf("[工作时间] 高位 '%c'=位%d, 低位 '%c'=位%d, 解码值=%d\r\n", f[0],
         (int)(p0 - TIME_DICT), f[1], (int)(p1 - TIME_DICT), wt);

  /* 换算成时间窗口；idx 超过 275 说明这个配置值根本没有对应的窗口 */
  {
    uint8_t ws = 0;
    uint8_t we = 0;

    if (indexToTimeWindow(wt, &ws, &we)) {
      uint8_t e = workWindowEndAdj(we);

      if (e >= 24) {
        /* 最大窗口按整天显示，结束点写成 24:00（即次日 00:00） */
        printf("[工作时间] 窗口: %02u:00 ~ 24:00 (共 %u 小时)\r\n",
               (unsigned int)ws, (unsigned int)(e - ws));
      } else {
        printf("[工作时间] 窗口: %02u:00 ~ %02u:00 (共 %u 小时)\r\n",
               (unsigned int)ws, (unsigned int)e, (unsigned int)(e - ws));
      }
    } else {
      printf("[工作时间] idx=%d 无对应窗口(合法范围 0~275)\r\n", wt);
    }
  }

  /* 当前时刻与判定结果，供核对自动开关行为 */
  if (dtuTimeValid) {
    /* 刻意不写成 ... ? "在区间内" : "在区间外"：
     * 中文末字节紧贴闭合引号，是 GBK 解码吞引号的高危写法 */
    printf("[工作时间] 当前=%02u 时, 处于工作区间内=%d (1=是, 0=否)\r\n",
           (unsigned int)getWorkClockHour(), isInWorkWindow() ? 1 : 0);
  } else {
    printf("[工作时间] 4G未对时, 视为全天在区间内(射频常开)\r\n");
  }
}

/**
 * @brief  获取当前实时时间戳（基于网络对时 + HAL 计时）
 * @note   对时后以 dtuTimestamp 为基准，加上经过的秒数
 */
static uint32_t getCurrentTimestamp(void) {
  if (!dtuTimeValid)
    return 0;
  return dtuTimestamp + (HAL_GetTick() - dtuSyncTick) / 1000;
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
 *         STM32F1 只能半字编程；用 i+1<=len 兜底，
 *         避免读到源串 '\0' 之后的越界字节
 * ===================================================================== */
static void flash_write_str(uint32_t addr, const char *str) {
  int len = (int)strlen(str);
  int i;
  for (i = 0; i <= len; i += 2) {
    uint16_t halfWord = (uint16_t)str[i];
    if (i + 1 <= len)
      halfWord |= ((uint16_t)str[i + 1]) << 8;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, halfWord);
  }
}

/* =====================================================================
 * @brief  整页重写：对时配置 + 农场中心坐标
 * @note   必须两项一起写：Flash 擦除以页为单位，
 *         只写其中一项会把同页里的另一项一起擦掉
 * ===================================================================== */
static void flash_save_all(void) {
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t pageError = 0;

  eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = CONFIG_FLASH_ADDR;
  eraseInit.NbPages = 1;

  HAL_FLASH_Unlock();
  HAL_FLASHEx_Erase(&eraseInit, &pageError);

  /* 对时配置区 */
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_FLASH_ADDR, CONFIG_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + 4, configStr);

  /* 农场中心坐标区 */
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                    CONFIG_FLASH_ADDR + CENTER_FLASH_OFF, CENTER_MAGIC);
  flash_write_str(CONFIG_FLASH_ADDR + CENTER_FLASH_OFF + 4, centerStr);

  HAL_FLASH_Lock();
  printf("[Flash] 已保存: config=%s, center=%s\r\n", configStr, centerStr);
}

/**
 * @brief  从 Flash 加载配置信息 + 农场中心坐标
 * @note   开机时调用，如果 Flash 中有有效数据则加载
 */
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

  /* LoRa 射频开关不落盘：默认就是可收发，重启后同样回到可收发 */
  loraSwOn = 1;
}

/**
 * @brief  将消息加入 DTU 发送队列（环形缓冲区）
 * @param  msg: 消息数据指针
 * @param  len: 消息长度
 * @note   队列满时丢弃最早的消息（覆盖队头）
 */
static void enqueueDtuMsg(const char *msg, int len) {
  if (len <= 0 || len >= DTU_MSG_MAX_LEN)
    return;

  __disable_irq();
  if (dtuQueueCount >= DTU_QUEUE_SIZE) {
    // 队列满，丢弃最早的一条（队头前移）
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
    printf("[DTU Queue] 队列满，丢弃旧消息\r\n");
  }
  // 拷贝消息到队尾
  memcpy(dtuQueue[dtuQueueTail].buf, msg, len);
  dtuQueue[dtuQueueTail].buf[len] = '\0';
  dtuQueue[dtuQueueTail].len = (uint16_t)len;
  dtuQueueTail = (dtuQueueTail + 1) % DTU_QUEUE_SIZE;
  dtuQueueCount++;
  __enable_irq();
}

/**
 * @brief  处理 DTU 发送队列：每 1 秒从队头取出一条消息发送
 * @note   在主循环中调用，非阻塞
 */
static void processDtuQueue(void) {
  if (dtuQueueCount == 0)
    return;

  if (HAL_GetTick() - dtuLastSendTick >= 500) {
    dtuLastSendTick = HAL_GetTick();

    // 从队头取出一条消息
    DtuQueueItem_t *item = &dtuQueue[dtuQueueHead];
    HAL_UART_Transmit(&huart3, (uint8_t *)item->buf, item->len, 1000);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2, 100);
    printf("[DTU 队列发送] %s\r\n", item->buf);

    // 出队
    dtuQueueHead = (dtuQueueHead + 1) % DTU_QUEUE_SIZE;
    dtuQueueCount--;
  }
}

/**
 * @brief  统一上报数据给 DTU（JSON格式）
 * @note   数据先入队，由 processDtuQueue() 每秒发送一条
 * @param  loraStr: lorainfo 字段内容
 * @param  rssi:    RSSI 信号强度
 * @param  snr:     SNR 信噪比
 */
static void sendToDtu(const char *loraStr, int rssi, int snr) {
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
                     loraStr, (char *)deviceName,
                     (unsigned long)getCurrentTimestamp(), rssi, snr);

  if (len > 0 && len < (int)sizeof(txBuf)) {
    enqueueDtuMsg(txBuf, len);
    printf("[DTU 入队] 队列=%d/%d\r\n", dtuQueueCount, DTU_QUEUE_SIZE);
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
 * @brief  上报农场中心坐标给 DTU
 * @note   数据格式: 1|deviceName|lat,lng|
 *         （MSG_TYPE_GPS = 1，与终端 GPS 上报类型一致）
 *         未设置过坐标时第 3 段上报 "0,0"，保证帧结构固定为 3 段，
 *         云端/小程序 split('|') 不会拿到空串
 * ===================================================================== */
static void sendCenterToDtu(void) {
  char dataStr[64];
  const char *coord = (centerStr[0] != '\0') ? centerStr : "0,0";

  snprintf(dataStr, sizeof(dataStr), "%d|%s|%s|", MSG_TYPE_GPS, deviceName,
           coord);
  printf("[DTU] 上报中心坐标: %s\r\n", dataStr);
  sendToDtu(dataStr, dtuCsqRssi, 0);
}

/**
 * @brief  缓存待执行指令（每个设备最多一条，新指令替换旧指令）
 * @param  deviceId: 目标设备 ID
 * @param  cmd:      指令内容
 * @param  val:      指令参数
 */
static void cachePendingCommand(const char *deviceId, const char *cmd,
                                const char *val) {
  int freeSlot = -1;

  /* 先查找是否已有该设备的缓存槽位 */
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      /* 已有该设备的缓存，直接替换 */
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

  /* 未找到已有槽位，使用空闲槽位 */
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

/**
 * @brief  检查并处理指定设备的待执行缓存指令
 * @note   当 LoRa 收到该设备的上报数据时调用，打印缓存指令并清除缓存
 * @param  deviceId: 上报数据的设备 ID
 */
static uint8_t checkPendingCommand(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      printf(
          "[PENDING CMD] Found pending cmd for device=%s: cmd=%s, val=%s\r\n",
          deviceId, pendingCmdCache[i].cmd, pendingCmdCache[i].val);
      return 1; /* 有缓存指令 */
    }
  }
  return 0; /* 无缓存指令 */
}

/**
 * @brief  通过 LoRa 发送缓存指令给目标设备并清除缓存
 * @param  deviceId: 目标设备 ID
 * @retval 1=已发送, 0=无缓存
 */
static uint8_t sendPendingCommand(const char *deviceId) {
  for (int i = 0; i < PENDING_CMD_MAX_SLOTS; i++) {
    if (pendingCmdCache[i].used &&
        strcmp(pendingCmdCache[i].deviceId, deviceId) == 0) {
      /* 通过 LoRa 下发指令给目标设备，格式: 9|deviceId|cmd|val| */
      char txData[64];
      int txLen = snprintf(txData, sizeof(txData), "%d|%s|%s|%s", MSG_TYPE_COM,
                           pendingCmdCache[i].deviceId, pendingCmdCache[i].cmd,
                           pendingCmdCache[i].val);
      uint32_t tx_time = 0;
      rf_single_tx_data((uint8_t *)txData, txLen, &tx_time);
      printf("[PENDING CMD] LoRa TX: %s\r\n", txData);

      /* 等待发送完成 */
      uint32_t waitStart = HAL_GetTick();
      while (rf_get_transmit_flag() != RADIO_FLAG_TXDONE) {
        if (HAL_GetTick() - waitStart > 3000)
          break;
      }
      rf_set_transmit_flag(RADIO_FLAG_IDLE);

      /* 清除该缓存槽位 */
      pendingCmdCache[i].used = 0;
      memset(&pendingCmdCache[i], 0, sizeof(PendingCmd_t));
      printf("[PENDING CMD] Cache cleared for device=%s, cmd delivered\r\n",
             deviceId);
      return 1;
    }
  }
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

/**
 * @brief  JSON 解析 DTU 下发的指令
 * @note   从外层 JSON 提取 cominfo 字段，反转义后解析内部 JSON
 *         内部格式: {"cmd":"xxx","value":"xxx","deviceId":"xxx"}
 */
static void parseDtuCommand(char *buf) {
  /* Step 1: 从外层 JSON 提取 cominfo 字段值（处理转义） */
  char *pCominfo = strstr(buf, "\"cominfo\":\"");
  if (!pCominfo) {
    printf("[DTU JSON] cominfo not found\r\n");
    return;
  }

  pCominfo += 11; // 跳过 "cominfo":"
  char unescaped[200];
  int ui = 0;

  // 读取 cominfo 字符串值，处理转义序列
  while (*pCominfo && ui < (int)sizeof(unescaped) - 1) {
    if (*pCominfo == '\\' && *(pCominfo + 1) != '\0') {
      char next = *(pCominfo + 1);
      if (next == '"' || next == '\\' || next == '/') {
        unescaped[ui++] = next;
        pCominfo += 2;
        continue;
      }
    } else if (*pCominfo == '"') {
      break; // 未转义的引号 = cominfo 字符串结束
    }
    unescaped[ui++] = *pCominfo++;
  }
  unescaped[ui] = '\0';
  printf("[DTU JSON] cominfo: %s\r\n", unescaped);

  /* Step 2: 从反转义后的 JSON 提取各字段 */
  char cmd[32] = {0}, devId[32] = {0}, val[32] = {0};

  // 提取 cmd
  char *p = strstr(unescaped, "\"cmd\":");
  if (p) {
    p += 6;
    while (*p == ' ' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < 31)
      cmd[i++] = *p++;
    cmd[i] = '\0';
  }

  // 提取 deviceId
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

  // 提取 value
  p = strstr(unescaped, "\"value\":");
  if (p) {
    p += 8;
    while (*p == ' ' || *p == '"')
      p++;
    int i = 0;
    while (*p && *p != '"' && i < 31)
      val[i++] = *p++;
    val[i] = '\0';
    // 去除 value 尾部多余的逗号
    int vlen = (int)strlen(val);
    while (vlen > 0 && val[vlen - 1] == ',')
      val[--vlen] = '\0';
  }

  printf("[DTU JSON] cmd=%s, deviceId=%s, value=%s\r\n", cmd, devId, val);

  /* debug 指令特殊处理：无论 deviceId 是否匹配都打印调试信息 */
  if (strcmp(cmd, "debug") == 0) {
    printf("[DTU CMD] 接收到了 debug 命令 (deviceId=%s, value=%s)\r\n", devId, val);
  }

  /* Step 3: 检查 deviceId 是否为本机 */
  if (strcmp(devId, (char *)deviceName) != 0) {
    printf("[DTU CMD] Not this device (target=%s, local=%s), cache it\r\n",
           devId, deviceName);
    cachePendingCommand(devId, cmd, val);
    return;
  }

  /* Step 4: 执行指令 */
  if (strcmp(cmd, "relayreboot") == 0) {
    printf("[DTU CMD] 执行设备重启...\r\n");
    HAL_Delay(500);
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
     printf("[DTU CMD] 接收到了debug命令...\r\n");
    /* debug: value=2 时复位 DTU 模块 */
    int debugVal = atoi(val);
    if (debugVal == 2) {
      printf("[DTU CMD] debug: 复位 DTU 模块 (PA3 拉低 100ms)\r\n");
      HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_RESET); // 拉低
      HAL_Delay(100);                                                     // 保持 100ms
      HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_SET);   // 拉高
      printf("[DTU CMD] DTU 复位完成\r\n");
    } else {
      printf("[DTU CMD] debug: value=%d (非 2，不执行复位)\r\n", debugVal);
    }
  } else if (strcmp(cmd, "A") == 0) {
    if (val[0] != '\0') {
      strncpy(configStr, val, sizeof(configStr) - 1);
      configStr[sizeof(configStr) - 1] = '\0';
      flash_save_all();
      printf("[DTU CMD] Config updated: %s\r\n", configStr);
      printWorkTimeDebug(); /* 配置变了，打印一次解码结果供核对 */
      HAL_Delay(1000);
      sendConfigToDtu();
      printf("[DTU CMD] 配置已上报\r\n");
    } else {
      printf("[DTU CMD] value 为空\r\n");
    }
  } else if (strcmp(cmd, "gpstm") == 0) {
    /* gpstm 中继指令：仅当 deviceId 匹配本机时激活，记录当前时间和有效时长 */
    if (strcmp(devId, (char *)deviceName) != 0) {
      printf("[DTU CMD] gpstm 目标设备不匹配(目标=%s, 本机=%s)，忽略\r\n",
             devId, deviceName);
      return;
    }
    gpstmActive = 1;
    gpstmRecvTick = HAL_GetTick();
    gpstmTimeoutMin = (uint32_t)atoi(val);
    if (gpstmTimeoutMin == 0)
      gpstmTimeoutMin = 30; // 默认 30 分钟
    printf("[DTU CMD] gpstm 中继已记录: device=%s, timeout=%lu min\r\n",
           deviceName, (unsigned long)gpstmTimeoutMin);
  } else if (strcmp(cmd, "lorasw") == 0) {
    /* lorasw 中继 LoRa 射频开关
     *   value=1 → 开启射频，恢复收发
     *   value=0 → 关闭射频，进入深度休眠（MCU 与 4G/DTU 仍工作）
     * 状态只存在于 RAM，不写 Flash：默认就是可收发，
     * 重启后同样回到可收发（掉电/复位相当于自动恢复）
     * 云端可经 4G 下发 lorasw|1 远程唤醒 */
    if (strcmp(devId, (char *)deviceName) != 0) {
      printf("[DTU CMD] lorasw 目标设备不匹配(目标=%s, 本机=%s)，忽略\r\n",
             devId, deviceName);
      return;
    }

    if (atoi(val) != 0) {
      /* 1 → 唤醒
       * @warning 工作时间优先：不在工作区间内时一律拒绝开启，
       *          lorasw 只能在区间内生效，不能越权把区间外的射频打开 */
      if (!isInWorkWindow()) {
        printf("[DTU CMD] lorasw: 当前不在工作区间内, 拒绝开启射频\r\n");
      } else if (loraSwOn) {
        printf("[DTU CMD] lorasw: 当前已是收发状态, 无需重复开启\r\n");
      } else {
        lora_rf_wake();
      }
    } else {
      /* 0 → 休眠（任何时候都允许关闭，不受工作区间限制） */
      if (!loraSwOn) {
        printf("[DTU CMD] lorasw: 当前已是休眠状态, 无需重复关闭\r\n");
      } else {
        lora_rf_sleep();
      }
    }
  } else if (strcmp(cmd, "txpower") == 0) {
    /* txpower: 缓存指令给目标设备，并立即重新获取信号强度和网络时间 */
    printf("[DTU CMD] txpower: target=%s, value=%s, cache it\r\n", devId, val);
    cachePendingCommand(devId, cmd, val);
    /* 将查询命令加入 DTU 发送队列（由 processDtuQueue 每 500ms 发送一条） */
    {
      const char *csqCmd = "config,get,csq";
      const char *nettimeCmd = "config,get,nettime";
      enqueueDtuMsg(csqCmd, strlen(csqCmd));
      enqueueDtuMsg(nettimeCmd, strlen(nettimeCmd));
      printf("[DTU CMD] 已将 CSQ + nettime 查询加入队列\r\n");
    }
    /* 重置查询定时器，后续按正常间隔查询 */
    dtuCsqTick = HAL_GetTick();
    return;
  } else if (strcmp(cmd, "realyname") == 0) {
    /* realyname 更换设备名字 */
    /* value 格式: "v3-10=v3-11"  →  目标设备=v3-10, 新名字=v3-11 */
    char *eq = strchr(val, '=');
    if (eq != NULL) {
      *eq = '\0';
      char *targetDevId = val;
      char *newName = eq + 1;
      printf("[DTU CMD] realyname: target=%s, newName=%s, cache it\r\n",
             targetDevId, newName);
      cachePendingCommand(targetDevId, cmd, newName);
    } else {
      printf("[DTU CMD] realyname: value 格式错误 (缺少 '=')\r\n");
    }


  } else {
    printf("[DTU CMD] 未知指令: %s\r\n", cmd);
  }
}

/* USER CODE END 0 */

/**
 * @brief  应用程序主入口函数
 * @retval int (实际不会返回)
 * @note   主函数执行流程：
 *         1. HAL 库初始化（复位所有外设、初始化 Flash 和 SysTick）
 *         2. 系统时钟配置（72MHz）
 *         3. 外设初始化（GPIO、USART1、USART2、NVIC）
 *         4. LoRa 模块初始化和参数配置
 *         5. 默认进入接收模式
 *         5. 主循环：LED 心跳、模式处理
 */
int main(void) {
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU 配置 --------------------------------------------------------*/

  /* 复位所有外设，初始化 Flash 接口和 SysTick 定时器 */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 配置系统时钟 */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 初始化所有配置的外设 */
  MX_GPIO_Init();        // GPIO 初始化（LED、按键、LoRa CSN/RST/IRQ）
  MX_SPI1_Init();        // SPI1 初始化（LoRa 硬件 SPI）
  MX_USART1_UART_Init(); // USART1 初始化（调试串口，115200 波特率）
  MX_USART3_UART_Init(); // USART3 初始化（DTU 通信，115200 波特率）

  /* 初始化 NVIC 中断控制器（LoRa IRQ 使用 PA2 EXTI） */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */

  // 打印启动信息
  printf("\r\n===== LoRa Receiver Start =====\r\n");
  printf("DTU UART (USART3): PB10=TX, PB11=RX, 115200 8N1\r\n");
  printf("USART3 CR1=0x%04X CR2=0x%04X CR3=0x%04X BRR=0x%04X SR=0x%04X\r\n",
         (uint16_t)huart3.Instance->CR1, (uint16_t)huart3.Instance->CR2,
         (uint16_t)huart3.Instance->CR3, (uint16_t)huart3.Instance->BRR,
         (uint16_t)huart3.Instance->SR);
  printf_logo(); // 显示工程信息 Logo

  /* 打印芯片唯一 ID 并根据 UID 生成 deviceName */
  {
    uint32_t uid0 = *(volatile uint32_t *)0x1FFFF7E8;
    uint32_t uid1 = *(volatile uint32_t *)0x1FFFF7EC;
    uint32_t uid2 = *(volatile uint32_t *)0x1FFFF7F0;
    printf("Chip UID    : %08X-%08X-%08X\r\n", (unsigned int)uid2,
           (unsigned int)uid1, (unsigned int)uid0);

    int devIndex = getDeviceIndex();
    if (devIndex >= 0) {
      snprintf(deviceName, sizeof(deviceName), "v3-%d", 100 + devIndex);
      printf("设备名称 Device Name : %s (index=%d)\r\n", deviceName,
             100 + devIndex);
    } else {
      printf("Device Name : %s (UID not in list, use default)\r\n", deviceName);
    }
  }

  /* 从 Flash 加载配置 */
  loadConfigFromFlash();

  /* 开机打印一次工作时间解码结果，供核对字典顺序是否正确 */
  printWorkTimeDebug();

  /* 测试公共模块 */
  commonTest_PrintInfo();
  printf("commonTest_Increment = %lu\r\n",
         (unsigned long)commonTest_Increment());

  HAL_Delay(1); // 短暂延时，等待串口输出完成

  /* LoRa PAN3029 模块初始化 */
  printf("LoRa init...\r\n");

  /* 确保 CSN 和 RST 引脚为高电平（LoRa 模块空闲状态） */
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET); // CSN 拉高
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET); // RST 拉高
  HAL_Delay(100); // 等待模块电源稳定

  // 初始化 LoRa 射频模块
  if (rf_init() != RF_OK) {
    // 第一次初始化失败，尝试 RST 复位后重试
    printf("[LoRa] 首次初始化失败，尝试 RST 复位...\r\n");
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET); // RST 拉低
    HAL_Delay(100);                                                       // 保持 100ms
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);   // RST 拉高
    HAL_Delay(100);                                                       // 等待模块稳定
    
    // 重新初始化
    if (rf_init() != RF_OK) {
      // 仍然失败，进入错误循环
      printf("[LoRa] RST 复位后仍初始化失败! Check wiring.\r\n");
      while (1) {
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin); // LED1 快速闪烁表示错误
        HAL_Delay(100);
      }
    }
    printf("[LoRa] RST 复位后初始化成功\r\n");
  }
  printf("RF init ok\r\n");

  /* 设置 LoRa 默认参数（与 CenterReceiveBLE_v4 ETSI_868 配置一致）：
   *   频率 = 915 MHz, 扩频因子 SF = 10, 带宽 BW = 125 kHz
   *   编码率 CR = 4/5, 发射功率 = 22 dBm
   * 此函数还会打印: FREQ= xxx  SF=x   BW=x  CR=x */
  rf_set_default_para();

  // 打印 LoRa 配置信息（读取 radio.h 实际配置）
  printf("===== LoRa Ready =====\r\n");
  printf("  Freq    : %d MHz\r\n", DEFAULT_FREQ / 1000000);
  printf("  SF      : %d\r\n", DEFAULT_SF);
  printf("  BW      : 125 kHz\r\n");
  printf("  CR      : 4/5\r\n");
  printf("  Power   : %d dBm\r\n", DEFAULT_PWR);
  printf("  CRC     : OFF\r\n");
  printf("  SyncWord: 0x12\r\n");
  printf("----------------------------\r\n");
  printf("  K1 -> RX mode (LED 500ms blink)\r\n");        // 按键 K1 功能说明
  printf("  K2 -> TX mode (send 'I LOVE YOU' / 3s)\r\n"); // 按键 K2 功能说明
  printf("----------------------------\r\n");

  /* LoRa 射频开关不落盘，开机恒为"可收发"，
   * 任何掉电/复位都自动恢复收发，无需远程唤醒 */
  enter_rx_mode(); // 默认进入接收模式

  /* 记录启动时间，用于 5 秒后向 DTU 发送 CSQ 查询 */
  dtuCsqTick = HAL_GetTick();

  /* USER CODE END 2 */

  /* 无限循环 */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* [调试] 每秒打印一次 STM32 实时时间 */
    if (dtuTimeValid && HAL_GetTick() - lastPrintTick >= 1000) {
      lastPrintTick = HAL_GetTick();
      char timeBuf[24];
      common_formatTimestamp(getCurrentTimestamp(), timeBuf, sizeof(timeBuf));
      // printf("[TIME] %s\r\n", timeBuf);
    }

    /* PB15 (LED1) 接收指示：点亮500ms后自动熄灭 */
    if (led1On && HAL_GetTick() - led1OnTick >= 500) {
      led1On = 0;
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    }

    /* DTU 发送队列处理：每秒从队列取出一条消息发送 */
    processDtuQueue();

    /* 工作区间自动开关射频（每秒检测一次，只在进出区间的那一刻动作）
     * 边沿触发而非全程强制：区间内手动 lorasw|0 关掉后会一直保持关闭，
     * 直到区间结束再进入才自动重开，手动关在区间内因此仍然有效
     * 工作区间优先级高于 lorasw 指令：区间外一律休眠 */
    if (HAL_GetTick() - workWinCheckTick >= 1000) {
      uint8_t inWin = isInWorkWindow();

      workWinCheckTick = HAL_GetTick();

      if (inWin != prevInWorkWindow) {
        if (inWin) {
          printf("[工作时间] 进入工作区间, 射频唤醒\r\n");
          if (!loraSwOn)
            lora_rf_wake();
        } else {
          printf("[工作时间] 离开工作区间, 射频休眠\r\n");
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

    default: // 空闲模式，不做任何处理
      break;
    }

    /* DTU 一帧完成判断：收到数据后 500ms 无新字节 或 缓冲区满 */
    if ((dtuRxLen > 0 && HAL_GetTick() - dtuLastRxTick > 500) ||
        dtuRxLen >= DTU_RX_BUF_SIZE) {
      /* 确保字符串结尾 */
      if (dtuRxLen < DTU_RX_BUF_SIZE)
        dtuRxBuf[dtuRxLen] = '\0';
      else
        dtuRxBuf[DTU_RX_BUF_SIZE - 1] = '\0';

      char *pBuf = (char *)dtuRxBuf;

      /* 解析 CSQ 信号质量: config,csq,ok,<value> */
      char *pCsq = strstr(pBuf, "config,csq,ok,");
      if (pCsq) {
        int csqVal = 0;
        if (sscanf(pCsq, "config,csq,ok,%d", &csqVal) == 1) {
          int percent = csqVal * 100 / 31;
          printf("[DTU] 信号强度: %d (%d%%)\r\n", csqVal, percent);
          dtuCsqRssi = csqVal; // 保存 CSQ 值供对时上报使用
        }
      }

      /* 解析网络时间: config,nettime,ok,年,月,日,时,分,秒,毫秒 */
      char *pNet = strstr(pBuf, "config,nettime,ok,");
      if (pNet) {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
        if (sscanf(pNet, "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", &y, &mo, &d,
                   &h, &mi, &s, &ms) >= 6) {
          printf("[DTU] 网络时间: %04d/%02d/%02d %02d:%02d:%02d\r\n", y, mo, d,
                 h, mi, s);
          /* 保存时间戳供 LoRa 上报使用 */
          snprintf(dtuNetTimeStr, sizeof(dtuNetTimeStr),
                   "%04d%02d%02d%02d%02d%02d", y, mo, d, h, mi, s);
          dtuTimestamp = dateTimeToTimestamp(y, mo, d, h, mi, s);
          dtuSyncTick = HAL_GetTick(); // 记录对时时刻
          printf("[DTU] Unix timestamp: %lu\r\n", (unsigned long)dtuTimestamp);
          dtuTimeValid = 1; // 标记网络时间已获取
          dtuNetTimeFailCount = 0;  /* 重置连续失败计数器 */
          printf("[DTU] 网络时间获取成功，切换为%lu分钟对时\r\n",
                 (unsigned long)(getConfigIntervalMs() / 60000));

          // 获取到正确时间后，立即上报一次对时信息（入队，由队列控制间隔）
          sendTimeSyncToDtu();
          // 开机后首次获取到网络时间，上报一次配置信息（仅上报一次）
          if (!configBootSent) {
            configBootSent = 1;
            sendConfigToDtu();
          }
        }
      }

      /* 都没匹配到，原样打印明文 */
      if (!pCsq && !pNet) {
        /* 收到 DTU 下发数据：先把当前生效的工作时间打出来，
         * 放在 parseDtuCommand() 之前，方便对照"指令执行前 / 执行后" */
        printWorkTimeDebug();

        /* JSON 解析 DTU 下发的指令 */
        parseDtuCommand(pBuf);
      }

      dtuRxLen = 0;
      /* 重新启动中断接收 */
      HAL_UART_Receive_IT(&huart3, dtuRxBuf, 1);
    }

    /* 向 DTU 发送 csq + nettime 查询
     * 未获取到时间前：每 5 秒查询一次
     * 获取到时间后：由 configStr 配置决定（默认 30 分钟，范围 5~60 分钟） */
    {
      uint32_t queryInterval = dtuTimeValid ? getConfigIntervalMs() : 5000;
      if (HAL_GetTick() - dtuCsqTick >= queryInterval) {
        dtuCsqTick = HAL_GetTick();
        uint8_t cmd1[] = "config,get,csq\r\n";
        HAL_UART_Transmit(&huart3, cmd1, sizeof(cmd1) - 1, 1000);
        HAL_Delay(300); // 等待 csq 响应
        uint8_t cmd2[] = "config,get,nettime\r\n";
        HAL_UART_Transmit(&huart3, cmd2, sizeof(cmd2) - 1, 1000);
        if (!dtuTimeValid) {
          dtuNetTimeFailCount++;
          printf("[DTU] Waiting for net time... (retry %lus, fail=%d/5)\r\n",
                 queryInterval / 1000, dtuNetTimeFailCount);
          /* 连续 5 次失败，重启 STM32 */
          if (dtuNetTimeFailCount >= 5) {
            printf("[DTU] 连续 5 次获取网络时间失败，重启 STM32...\r\n");
            HAL_Delay(500);
            NVIC_SystemReset();
          }
        }
      }
    }

    HAL_Delay(1); // 短延时
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
 * @brief  GPIO 初始化函数
 * @note   配置说明：
 *         1. LED 指示灯（本板仅有 1 个）：
 *            - LED1 (PB15) - 输出，推挽，低速，默认灭
 *                            收到 LoRa 数据时点亮，500ms 后由主循环熄灭；
 *                            LoRa 初始化失败时快速闪烁报错
 *         2. 按键输入：
 *            - 本板无按键（只有复位键）
 *            - 原 K1(PB4)/K2(PB5) 的引脚定义、GPIO 配置、NVIC 使能与
 *              EXTI4/EXTI9_5 中断服务函数均已删除
 *         3. LoRa SPI 引脚（软件模拟 SPI）：
 *            - SCK (PA5) - 输出，推挽，高速
 *            - MOSI (PA7) - 输出，推挽，高速
 *            - MISO (PA6) - 输入，无上下拉
 *         4. LoRa 控制引脚：
 *            - CSN (PB0) - 输出，推挽，高速，默认高电平（未选中）
 *            - RST (PA1) - 输出，推挽，高速，默认高电平（不复位）
 *         5. LoRa 中断引脚：
 *            - IRQ (PA2) - 外部中断，上升沿触发，下拉
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
  HAL_GPIO_WritePin(GPIOB, LED1_Pin, GPIO_PIN_RESET); // LED1 默认灭

  /* 配置 LED1 引脚 (PB15) —— 本板唯一的 LED */
  GPIO_InitStruct.Pin = LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;          // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // 低速
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 按键 K1(PB4)/K2(PB5) 本板已无此引脚，引脚配置与 EXTI 使能整体删除 */

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* 配置 LoRa SPI 引脚 (PA5/PA6/PA7) - 硬件 SPI1 复用推挽输出 */
  GPIO_InitStruct.Pin = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;       // 复用推挽输出（硬件 SPI）
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高速
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 配置 LoRa CSN 片选引脚 (PB0) - 默认高电平（未选中） */
  GPIO_InitStruct.Pin = LORA_CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高速
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET); // CSN 拉高

  /* 配置 LoRa RST 复位引脚 (PA1) - 默认高电平（不复位） */
  GPIO_InitStruct.Pin = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;           // 无上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高速
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET); // RST 拉高

  /* 配置 DTU RST 复位引脚 (PA3) - 默认高电平（不复位） */
  GPIO_InitStruct.Pin = DTU_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DTU_RST_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(DTU_RST_GPIO_Port, DTU_RST_Pin, GPIO_PIN_SET); // 默认高电平

  /* 配置 LoRa IRQ 中断引脚 (PA2) - 上升沿触发，下拉 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING; // 外部中断，上升沿触发
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;       // 下拉（默认低电平）
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/**
 * @brief  SPI1 初始化函数（LoRa PAN3029 硬件 SPI）
 * @note   配置 SPI1 外设：
 *         - 主机模式，全双工，8-bit 数据
 *         - CPOL=0, CPHA=0（空闲时 SCK 低电平，第一边沿采样）
 *         - MSB 优先，软件 NSS，波特率分频 16（72MHz/16 = 4.5MHz）
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
 * @brief  NVIC 中断控制器初始化函数（LoRa IRQ）
 * @note   配置 LoRa 模块的 IRQ 引脚 (PA2) 对应的外部中断 EXTI2
 *         - 中断优先级: 抢占优先级 0，子优先级 0（最高优先级）
 *         - 触发方式: 上升沿触发（在 GPIO 初始化中配置）
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
 * @note   当 EXTI 中断触发时，HAL 库会调用此函数
 *         根据中断引脚判断中断源，执行相应处理
 * @param  GPIO_Pin: 触发中断的引脚号
 * @retval None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  /* LoRa IRQ 中断: PA2 -> 调用射频中断处理函数 */
  if (GPIO_Pin == GPIO_PIN_2) {
    rf_irq_process(); // 处理 LoRa 模块的中断事件
  }

  /* 本板无按键，K1(PB4)/K2(PB5) 已从工程彻底删除
   * （引脚定义、GPIO 配置、NVIC 使能、EXTI4/EXTI9_5 中断服务函数全部移除）
   * 若以后新板要加按键，需三处一起补回：
   *   1) main.h 的 K1_Pin/K2_Pin 定义
   *   2) MX_GPIO_Init 的 IT_FALLING + PULLUP 配置与 NVIC 使能
   *   3) stm32f1xx_it.c 的 EXTI4_IRQHandler / EXTI9_5_IRQHandler
   * 且射频操作必须放在主循环，绝不能在中断里调用（按键 EXTI 优先级 0，
   * SysTick 优先级 15，中断里调 HAL_Delay 会整机卡死） */
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

  /* 启动中断接收，每个字节触发一次回调 */
  HAL_UART_Receive_IT(&huart3, dtuRxBuf, 1);
}

/**
 * @brief  USART3 接收完成回调（HAL 库弱函数重写）
 * @note   每收到 1 字节触发一次，记录时间戳并续接缓冲区
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART3) {
    dtuRxLen++;
    dtuLastRxTick = HAL_GetTick(); // 记录收到字节的时间
    if (dtuRxLen < DTU_RX_BUF_SIZE) {
      HAL_UART_Receive_IT(&huart3, &dtuRxBuf[dtuRxLen], 1);
    } else {
      dtuRxLen = 0; // 缓冲区满，丢弃
    }
  }
}

/**
 * @brief  错误处理函数
 * @note   当系统发生严重错误时调用此函数
 *         关闭所有中断，进入死循环
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq(); // 关闭所有中断
  while (1) {
    // 死循环，等待硬件复位
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  断言失败回调函数
 * @note   当 assert_param 检查失败时调用此函数
 *         可以在此处添加调试信息，输出文件名和行号
 * @param  file: 指向源文件名的指针
 * @param  line: 断言失败的源代码行号
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* 可以在此处添加调试输出，例如：
   * printf("Wrong parameters value: file %s on line %d\r\n", file, line);
   */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
