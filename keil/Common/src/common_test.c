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
#include "stdlib.h"

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

/**
  * @brief  将日期时间转换为 Unix 时间戳（内部辅助函数）
  */
static uint32_t _dateTimeToTimestamp(int year, int month, int day, int hour, int min, int sec)
{
  static const int daysBeforeMonth[] = {0,0,31,59,90,120,151,181,212,243,273,304,334};
  uint32_t days, timestamp;
  int y;

  y = year - 1;
  days = (uint32_t)(year - 1970) * 365
       + (y/4 - y/100 + y/400) - (1969/4 - 1969/100 + 1969/400)
       + daysBeforeMonth[month] + day - 1;
  if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days++;
  timestamp = days * 86400UL + (uint32_t)hour * 3600 + (uint32_t)min * 60 + (uint32_t)sec;
  return timestamp;
}

/**
  * @brief  替换 LoRa 对时消息中的时间字段为完整时间戳
  * @param  input:     原始消息 (如 "2|v4-18|31|99|1")
  * @param  output:    输出缓冲区
  * @param  outputSize: 输出缓冲区大小
  * @param  currentTs:  STM32 当前时间戳（用于补充日期）
  * @retval 0=成功, -1=失败
  */
int common_replaceTimeSegment(const char *input, char *output, int outputSize, uint32_t currentTs, uint32_t *outTimestamp)
{
  const char *pSeg1, *pSeg2, *pSeg3, *pSeg3End;
  char timeBuf[32];
  int timeLen, prefixLen, suffixLen;
  uint8_t isNumeric, len;
  uint32_t fullTs = 0;
  const char *p;

  if (outTimestamp) *outTimestamp = 0;
  if (!input || !output || outputSize <= 0) return -1;

  // 找到第1个 '|'
  pSeg1 = strchr(input, '|');
  if (!pSeg1) goto copy_original;

  // 找到第2个 '|'
  pSeg2 = strchr(pSeg1 + 1, '|');
  if (!pSeg2) goto copy_original;

  // 第3段开始
  pSeg3 = pSeg2 + 1;
  pSeg3End = strchr(pSeg3, '|');
  if (!pSeg3End) goto copy_original;

  // 提取第3段内容
  timeLen = pSeg3End - pSeg3;
  if (timeLen <= 0 || timeLen >= (int)sizeof(timeBuf)) goto copy_original;
  memcpy(timeBuf, pSeg3, timeLen);
  timeBuf[timeLen] = '\0';

  // 检查是否全是数字
  isNumeric = 1;
  len = 0;
  p = timeBuf;
  while (*p)
  {
    if (*p < '0' || *p > '9') { isNumeric = 0; break; }
    len++;
    p++;
  }
  if (!isNumeric || len == 0) goto copy_original;

  if (len >= 10)
  {
    // 完整时间戳，直接使用
    fullTs = (uint32_t)atol(timeBuf);
  }
  else if (currentTs > 0)
  {
    // 仅秒数，用 currentTs 的日期补充
    int totalSec = atoi(timeBuf);
    int hour = totalSec / 3600;
    int min = (totalSec % 3600) / 60;
    int sec = totalSec % 60;

    // 从 currentTs 提取年月日
    uint32_t days = currentTs / 86400;
    uint32_t year = 1970, month = 1, day = 1;
    uint32_t leapYear;
    static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};

    while (1)
    {
      leapYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
      uint16_t diy = 365 + leapYear;
      if (days < diy) break;
      days -= diy;
      year++;
    }
    for (month = 1; month <= 12; month++)
    {
      uint8_t dim = daysInMonth[month - 1];
      if (month == 2 && leapYear) dim = 29;
      if (days < (uint32_t)dim) break;
      days -= dim;
    }
    day = days + 1;

    fullTs = _dateTimeToTimestamp(year, month, day, hour, min, sec);
  }
  else
  {
    goto copy_original;
  }

  // 构建替换后的字符串: 前段 + 时间戳 + 后段
  {
    char tsStr[16];
    int tsLen = snprintf(tsStr, sizeof(tsStr), "%lu", (unsigned long)fullTs);

    prefixLen = pSeg3 - input;
    suffixLen = strlen(pSeg3End);

    if (prefixLen + tsLen + suffixLen >= outputSize) return -1;

    memcpy(output, input, prefixLen);
    memcpy(output + prefixLen, tsStr, tsLen);
    memcpy(output + prefixLen + tsLen, pSeg3End, suffixLen);
    output[prefixLen + tsLen + suffixLen] = '\0';
  }
  if (outTimestamp) *outTimestamp = fullTs;
  return 0;

copy_original:
  // 解析失败，直接拷贝原始数据
  if ((int)strlen(input) >= outputSize) return -1;
  strcpy(output, input);
  return -1;
}
