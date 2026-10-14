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
