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
#define TNOW_Pin GPIO_PIN_4
#define TNOW_GPIO_Port GPIOA
#define LED1_Pin GPIO_PIN_15
#define LED1_GPIO_Port GPIOB
/* 本板仅 PB15 一个 LED：LED2(PB13)/LED3(PB14) 已无此硬件，定义一并删除
 * LED4(PA0) 原本只存在于注释中，从未有实际定义 */
/* K1(PB4)/K2(PB5) 已从本板移除，引脚定义与中断配置一并删除 */

/* USER CODE BEGIN Private defines */
/* --- LoRa (PAN3029) 软件 SPI 引脚 --- */
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

/* --- DTU 复位引脚 (PA3) --- */
#define DTU_RST_Pin       GPIO_PIN_3
#define DTU_RST_GPIO_Port GPIOA
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
