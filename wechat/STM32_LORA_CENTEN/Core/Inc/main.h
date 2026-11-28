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
void centen_watchdog_start_early(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define TNOW_Pin GPIO_PIN_4
#define TNOW_GPIO_Port GPIOA
#define DTU_PWR_Pin       GPIO_PIN_15
#define DTU_PWR_GPIO_Port GPIOB
/* PB15 = DTU çµæº MOSï¼å LED1 æ¥æ¶æç¤ºç¯ï¼LED åè½åæ¶ï¼ï¼
 * å®æµç¡®è®¤ MOS ä¸ºä½çµå¹³å¯¼éï¼DTU_PWR_ON_LEVELï¼ä½ï¼= DTU å¸¸éçµï¼
 * åºä»¶ä»»ä½è·¯å¾é½ä¸å¾ä¸»å¨æ­ DTU çµ */
#define DTU_PWR_ON_LEVEL  GPIO_PIN_RESET
#define DTU_PWR_OFF_LEVEL GPIO_PIN_SET
/* 工作区间门控 DTU：1=仅工区间内给 DTU 供电并上报（区间外 PB15 断电、
 *    不上报 DTU 数据、暂停 PING/CSQ/UART 看门狗及复位重启链）；
 *    0=回退旧行为（DTU 常开常报）。区间判断复用工作窗逻辑 isInWorkWindow()，
 *    未对时/无工作时间字段/idx 越界时视为"在区间内"，保证开机必能对时 */
#define DTU_WIN_GATE 1
#define DTU_BOOT_GRACE_MS 600000u /* 开机/复位后前 10 分钟强制视为在区间内（保证重启后 DTU 能通信对时） */
/* 本板仅 PB15 一个 LED：LED2(PB13)/LED3(PB14) 已无此硬件，定义一并删除
 * LED4(PA0) 原本只存在于注释中，从未有实际定义 */
/* K1(PB4)/K2(PB5) 已从本板移除，引脚定义与中断配置一并删除 */

/* USER CODE BEGIN Private defines */
/* --- LoRa PAN3029 hardware SPI1 pins --- */
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

/* --- DTU 复位引脚 (PA3，高电平有效：拉高 100ms 复位，平时拉低) ---
 * 实测确认：平时高会把 DTU 摁死在复位态（无法联网）。
 * 两板统一"外设复位"脚约定：CENTEN=DTU RST（高有效），
 * ONLY_SEND=GPS RESET_N（低有效，极性相反、代码独立） */
#define DTU_RST_Pin       GPIO_PIN_3
#define DTU_RST_GPIO_Port GPIOA
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
