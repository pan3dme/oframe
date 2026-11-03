#ifndef _DTU_H_
#define _DTU_H_

#include <stdint.h>

/* 调试打印与 DTU 协议共用 USART1(PA9/PA10)：
 * DTU_PROD_MODE = 0 桌面调试（{usb}/[dtu] 前缀，对时失败不重启）
 *               = 1 正式装机（纯协议、丢弃调试打印，对时失败 5 次重启） */
#define DTU_PROD_MODE 1

/* 设备名：默认值，可在 main.c 启动时按芯片 UID 覆盖（见 device_name_init） */
#define DEVICE_NAME_DEFAULT "v4-F03"
extern char deviceName[16];
#define DEVICE_NAME deviceName /* DTU 上报 / LoRa 下发共用 */

/* 消息类型（与 ESP32 / CENTEN 对齐） */
#define MSG_TYPE_TIME     2   /* 定时上报（时间戳） */
#define MSG_TYPE_GPS      1   /* GPS 上报 */
#define MSG_TYPE_COM      9   /* 指令消息 */
#define MSG_TYPE_CONFIG   6   /* 配置消息 */
#define MSG_TYPE_SYN_TIME 4   /* 下发对时信息 */
#define MSG_TYPE_WARN     8   /* 告警消息 */
#define MSG_TYPE_UP_GPS   5   /* GPS 上传 */

/* DTU 协议层 */
void    dtu_init(void);                       /* 加载 Flash 配置，初始化定时器 */
void    dtu_poll(void);                       /* 主循环：队列/帧解析/周期查询 */
void    dtu_uart_rx_byte(uint8_t b);          /* 串口中断收 1 字节 */
void    dtu_report_lora(const char *loraStr, int rssi, int snr); /* JSON 上报入队 */
int     dtu_replace_time_segment(const char *src, char *dst, int dstSize);
int     dtu_expand_coord(const char *src, char *dst, int dstSize);

/* LoRa 下行接口：均为被动回复，只在 OnSlave() 延时调用，不可主动调用 */
uint8_t dtu_check_pending(const char *deviceId);
uint8_t dtu_gpstm_active(void);
uint8_t lora_reply_pending_cmd(const char *deviceId);
void    lora_reply_upgps(const char *deviceId);
void    lora_reply_time_sync(void);

/* 工作时段："2,0M" → 2 分钟查询间隔 + 00:00~22:00 时段。
 * 未对时 / 无字段 / idx 越界 一律视为在工作时段内（射频常开） */
uint8_t dtu_in_work_window(void);
void    dtu_print_work_window(const char *tag);

/* 由 main.c 提供 */
extern uint32_t getCurrentTimestamp(void);
void    app_set_synced_time(uint32_t ts);
void    app_lora_switch(uint8_t on);
uint8_t app_lora_is_on(void);

#endif
