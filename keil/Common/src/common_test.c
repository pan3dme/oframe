/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : common_test.c
  * @brief          : 公共测试模块实现文件
  * @note           : 此文件放在 Common/src/ 目录下，多个项目共享引用
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "common_test.h"
#include "stdio.h"
#include "string.h"

/* Private variables ---------------------------------------------------------*/
uint32_t commonTestCounter = 0;   // 公共测试计数器
uint8_t  commonTestFlag = 0;      // 公共测试标志位

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  公共测试函数：计数器自增并返回当前值
  * @param  None
  * @retval 当前计数器值
  */
uint32_t commonTest_Increment(void)
{
  commonTestCounter++;
  commonTestFlag = 1;
  return commonTestCounter;
}

/**
  * @brief  公共测试函数：打印模块信息
  * @param  None
  * @retval None
  */
void commonTest_PrintInfo(void)
{
  printf("\r\n===== Common Test Module =====\r\n");
  printf("  Version : %s\r\n", COMMON_TEST_VERSION);
  printf("  Counter : %lu\r\n", (unsigned long)commonTestCounter);
  printf("  Flag    : %d\r\n", commonTestFlag);
  printf("==============================\r\n");
}

/**
  * @brief  将 Unix 时间戳格式化为 "YYYY/MM/DD HH:MM:SS" 字符串
  * @param  timestamp: Unix 时间戳（秒，基于 1970-01-01 00:00:00 UTC）
  * @param  buf:       输出缓冲区
  * @param  bufSize:   缓冲区大小（建议 >= 20）
  * @retval 写入的字符数
  */
int common_formatTimestamp(uint32_t timestamp, char *buf, uint8_t bufSize)
{
  uint32_t seconds, minutes, hours, days;
  uint32_t year, month, day;
  uint32_t remaining;
  int len;

  // 每月天数累加表（非闰年）
  static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};

  // 计算时分秒
  remaining = timestamp % 86400;   // 当天经过的秒数
  hours     = remaining / 3600;
  minutes   = (remaining % 3600) / 60;
  seconds   = remaining % 60;

  // 计算年月日
  days = timestamp / 86400;        // 从 1970-01-01 起的天数
  year = 1970;

  while (1)
  {
    uint32_t daysInYear = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
    if (days < daysInYear) break;
    days -= daysInYear;
    year++;
  }

  // 判断闰年二月天数
  uint8_t febDays = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 29 : 28;

  month = 0;
  while (month < 12)
  {
    uint8_t dm = (month == 1) ? febDays : daysInMonth[month];
    if (days < dm) break;
    days -= dm;
    month++;
  }
  day = days + 1;
  month += 1;  // 0-based → 1-based

  // 格式化输出
  len = snprintf(buf, bufSize, "%04lu/%02lu/%02lu %02lu:%02lu:%02lu",
                 (unsigned long)year, (unsigned long)month, (unsigned long)day,
                 (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
  return len;
}
