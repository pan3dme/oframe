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
 * @note  已弃用休眠模式：模块改由 PB15 直接控制电源通断，
 *        此脚配为高阻输入不驱动（模块内部上拉决定，上电即 Full on） */
#define GNSS_WAKE_Pin        GPIO_PIN_1
#define GNSS_WAKE_GPIO_Port  GPIOB

/* --- GPS 电源开关 (PB15) ---
 * 低电平 = GPS 上电（开机、搜星）
 * 高电平 = GPS 断电（完全关机，0 电流） */
#define GPS_PWR_Pin          GPIO_PIN_15
#define GPS_PWR_GPIO_Port    GPIOB

/* ONLY_SEND 项目使用低功率 (16 dBm) */
#define DEFAULT_PWR            16
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
