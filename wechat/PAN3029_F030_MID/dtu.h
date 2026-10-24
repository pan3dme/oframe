#ifndef _DTU_H_
#define _DTU_H_

#include <stdint.h>

/* =====================================================================
 * 串口架构：调试打印与 DTU 协议共用 USART1(PA9/PA10)，不改动任何引脚
 *
 * DTU_PROD_MODE : 0 = 桌面调试（调试打印带 {usb} 前缀，协议流带 [dtu] 前缀，
 *                     对时失败 5 次不重启）
 *                 1 = 正式装机（串口纯跑 DTU 协议：无前缀、丢弃调试打印，
 *                     对时失败 5 次自动重启，同 CENTEN）
 * ===================================================================== */
#define DTU_PROD_MODE 1

/* ========================== 设备信息 ========================== */
#define DEVICE_NAME  "v4-F03"   /* 设备名称（DTU 上报 / LoRa 下发共用） */

/* ========================== 消息类型定义（与 ESP32 / CENTEN 对齐） ========================== */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_WARN     8   /* 告警消息 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* ========================== DTU 协议层接口（USART1 上云） ========================== */
void    dtu_init(void);                       /* 加载 Flash 配置，初始化查询定时器 */
void    dtu_poll(void);                       /* 主循环调用：队列发送/帧解析/周期查询 */
void    dtu_uart_rx_byte(uint8_t b);          /* 串口中断收到 1 字节时调用 */
void    dtu_report_lora(const char *loraStr, int rssi, int snr); /* JSON 上报入队 */
int     dtu_replace_time_segment(const char *src, char *dst, int dstSize); /* 替换时间字段 */

/* ========================== LoRa 回复接口（下行给子设备） ==========================
 * 这三个都是"被动回复"：只在收到子设备上报后，由 main.c OnSlave() 延时
 * 一次调用（见 main.c 的 LORA_REPLY_DELAY_*_MS），不可主动调用。
 * ================================================================================ */
uint8_t dtu_check_pending(const char *deviceId);    /* 该设备是否有缓存指令 */
uint8_t dtu_gpstm_active(void);                     /* gpstm 中继是否在有效期内 */
uint8_t lora_reply_pending_cmd(const char *deviceId); /* 回复：下发缓存指令并清除 */
void    lora_reply_upgps(const char *deviceId);       /* 回复：下发 upgps 指令 */
void    lora_reply_time_sync(void);                   /* 回复：下发对时消息 */

/* ========================== 由 main.c 提供 ========================== */
extern uint32_t getCurrentTimestamp(void);    /* 当前 Unix 时间戳 */
void app_set_synced_time(uint32_t ts);        /* DTU 网络时间写入主时间源 */

#endif
