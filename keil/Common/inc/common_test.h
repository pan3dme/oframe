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

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_TEST_H */
