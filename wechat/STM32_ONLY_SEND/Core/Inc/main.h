/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */
/* --- LoRa (PAN3029) 硬件 SPI1 引脚 --- */
#define LORA_SCK_Pin      GPIO_PIN_5
#define LORA_SCK_GPIO_Port GPIOA
#define LORA_MISO_Pin     GPIO_PIN_6
#define LORA_MISO_GPIO_Port GPIOA
#define LORA_MOSI_Pin     GPIO_PIN_7
#define LORA_MOSI_GPIO_Port GPIOA
#define LORA_CSN_Pin      GPIO_PIN_0
#define LORA_CSN_GPIO_Port GPIOB
#define LORA_RST_Pin      GPIO_PIN_1
#define LORA_RST_GPIO_Port GPIOA

/* --- GNSS 唤醒脚 (PB1 → GPS 模块 WAKE_UP) ---
 * @note  推挽输出，与 PB15 电源开关配合使用：
 *        搜星前先拉高 (Full on) 再上电，搜完拉低进 Standby(20uA 热启动)；
 *        初始低电平。模块Standby期间再上电=热启动，2 秒内出定位 */
#define GNSS_WAKE_Pin        GPIO_PIN_1
#define GNSS_WAKE_GPIO_Port  GPIOB

/* --- GPS 电源开关 (PB15) ---
 * 低电平 = GPS 上电（开机、搜星）
 * 高电平 = GPS 断电（完全关机，0 电流） */
#define GPS_PWR_Pin          GPIO_PIN_15
#define GPS_PWR_GPIO_Port    GPIOB

/* --- GPS 复位脚 (PA3 → L76K RESET_N) ---
 * 两板统一"外设复位"脚约定：CENTEN=DTU RST，ONLY_SEND=GPS RESET；
 * 推挽输出，平时高（不复位），拉低 100ms = 硬复位（热复位保星历，
 * 比 PB15 断电再上电的冷启动快得多）。当前仅初始化电平，
 * 触发逻辑待接好线后再加 */
#define GPS_RST_Pin          GPIO_PIN_3
#define GPS_RST_GPIO_Port    GPIOA

/* ONLY_SEND 项目使用低功率 (16 dBm) */
#define DEFAULT_PWR            16
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
