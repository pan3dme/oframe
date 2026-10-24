/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : common_test.h
  * @brief          : 公共测试模块头文件
  * @note           : 此文件放在 Common/inc/ 目录下，多个项目共享引用
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __COMMON_TEST_H
#define __COMMON_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define COMMON_TEST_VERSION  "V1.0"

/* Exported types ------------------------------------------------------------*/
typedef enum {
  MSG_TYPE_GPS = 1,         // GPS定位信息
  MSG_TYPE_TIME = 2,        // 对时信息
  MSG_TYPE_BATTERY = 3,     // 电量信息（小数，如0.5、0.1）
  MSG_TYPE_SYN_TIME = 4,    // 下发对时信息
  MSG_TYPE_UP_GPS = 5,      //   上报GPS跟踪坐标
  MSG_TYPE_CONFIG = 6,      // 配置信息
  MSG_TYPE_SYN_UP_TIME = 7, //   上报对时信息
  MSG_TYPE_WARN = 8,        // 警告消息
  MSG_TYPE_COM = 9          // 下载指令到设备
} MessageType_t;

/* Exported variables --------------------------------------------------------*/
extern uint32_t commonTestCounter;   // 公共测试计数器
extern uint8_t  commonTestFlag;      // 公共测试标志位

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  公共测试函数：计数器自增并返回当前值
  * @param  None
  * @retval 当前计数器值
  */
uint32_t commonTest_Increment(void);

/**
  * @brief  公共测试函数：打印模块信息
  * @param  None
  * @retval None
  */
void commonTest_PrintInfo(void);

/**
  * @brief  将 Unix 时间戳格式化为 "YYYY/MM/DD HH:MM:SS" 字符串
  * @param  timestamp: Unix 时间戳（秒）
  * @param  buf:       输出缓冲区
  * @param  bufSize:   缓冲区大小（建议 >= 20）
  * @retval 写入的字符数
  */
int common_formatTimestamp(uint32_t timestamp, char *buf, uint8_t bufSize);

/**
  * @brief  替换 LoRa 对时消息中的时间字段为完整时间戳
  * @param  input:       原始消息 (如 "2|v4-18|31|99|1")
  * @param  output:      输出缓冲区
  * @param  outputSize:  输出缓冲区大小
  * @param  currentTs:   STM32 当前时间戳（用于补充日期）
  * @param  outTimestamp: 输出的完整时间戳值（可为 NULL）
  * @retval 0=成功, -1=失败
  * @note   如果第3段是完整时间戳(>=10位)直接使用；如果是秒数(<10位)则用currentTs的日期补充
  */
int common_replaceTimeSegment(const char *input, char *output, int outputSize, uint32_t currentTs, uint32_t *outTimestamp);

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_TEST_H */
