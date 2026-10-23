#ifndef _DTU_H_
#define _DTU_H_

#include <stdint.h>

/* =====================================================================
 * 串口用途选择（编译期标记，烧录前修改 SERIAL_MODE 一行后重新编译）
 * 两种模式均使用 USART1(PA9/PA10)，不改动任何引脚
 *
 * SERIAL_MODE_DEBUG : 开发测试阶段，串口接电脑串口助手
 *                     - 调试打印每行自动加 {usb} 前缀
 *                     - DTU 协议流每帧自动加 [dtu] 前缀
 *                     - 可由串口助手模拟 DTU 下发指令测试解析
 *                     - 不主动周期查询 csq/nettime，不重启
 * SERIAL_MODE_DTU   : 调试完成后，串口接 DTU 设备
 *                     - 协议流原样收发，无前缀
 *                     - 调试打印全部丢弃
 *                     - 主动周期查询 csq/nettime，对时失败 5 次自动重启
 * ===================================================================== */
#define SERIAL_MODE_DEBUG 0
#define SERIAL_MODE_DTU   1
#define SERIAL_MODE       SERIAL_MODE_DEBUG   /* <-- 烧录标记：改这里 */

/* ========================== 设备信息 ========================== */
#define DEVICE_NAME  "v3-107"   /* 设备名称（DTU 上报 / LoRa 下发共用） */

/* ========================== 消息类型定义（与 ESP32 / CENTEN 对齐） ========================== */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息（中继→本设备） */
#define MSG_TYPE_WARN     8   /* 告警消息 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* ========================== DTU 协议层接口 ========================== */
void    dtu_init(void);                       /* 加载 Flash 配置，初始化查询定时器 */
void    dtu_poll(void);                       /* 主循环调用：队列发送/帧解析/周期查询 */
void    dtu_uart_rx_byte(uint8_t b);          /* 串口中断收到 1 字节时调用 */
void    dtu_report_lora(const char *loraStr, int rssi, int snr); /* JSON 上报入队 */
int     dtu_replace_time_segment(const char *src, char *dst, int dstSize); /* 替换时间字段 */
uint8_t dtu_check_pending(const char *deviceId);  /* 是否有缓存指令 */
uint8_t dtu_send_pending(const char *deviceId);   /* LoRa 下发缓存指令并清除 */
uint8_t dtu_gpstm_active(void);                   /* gpstm 中继是否在有效期内 */
void    dtu_send_upgps(const char *deviceId);     /* LoRa 下发 upgps 指令 */
void    dtu_send_time_reply(void);                /* LoRa 下发对时消息 */

/* ========================== 由 main.c 提供 ========================== */
extern uint32_t getCurrentTimestamp(void);    /* 当前 Unix 时间戳 */
void app_set_synced_time(uint32_t ts);        /* DTU 网络时间写入主时间源 */

#endif
