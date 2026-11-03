#ifndef _GPIO_H_
#define _GPIO_H_


#define  RF_SCK_PORT       GPIOB
#define  RF_SCK_IO         GPIO_Pin_13
#define  RF_SCK_AF         GPIO_PinSource13

#define  RF_MISO_PORT      GPIOB
#define  RF_MISO_IO        GPIO_Pin_14
#define  RF_MISO_AF        GPIO_PinSource14

#define  RF_MOSI_PORT      GPIOB
#define  RF_MOSI_IO        GPIO_Pin_15
#define  RF_MOSI_AF        GPIO_PinSource15

#define  RF_CSN_PORT       GPIOB
#define  RF_CSN_IO         GPIO_Pin_12

/* PAN3029 硬件复位脚（NRST，低电平有效），接 MCU PB0
 * 上电默认输出高=芯片正常工作；初始化前给一个低脉冲确保芯片从干净状态启动 */
#define  RF_RST_PORT       GPIOB
#define  RF_RST_IO         GPIO_Pin_0

#define  RF_IRQ_PORT       GPIOA
#define  RF_IRQ_IO         GPIO_Pin_1

#define  CAD_PORT          GPIOA
#define  CAD_IO            GPIO_Pin_0

#define LED1_PORT          GPIOA
#define LED1_PIN           GPIO_Pin_15

//底板切换主从模式选择引脚 
#define RF_MODE_PIN        GPIO_Pin_2
#define RF_MODE_PORT       GPIOA

/* 底板按键（低电平有效，按下 = GND）
 *   S1 : 硬件复位 NRST（直接重启芯片，软件无需参与）
 *   S2 → PB6：复位键，按下 20ms 消抖后 NVIC_SystemReset() 重启 MCU
 *   S3 → PB7：不使用（与 LED2 复用），保持上拉输入，不做任何处理 */
#define KEY1_PORT          GPIOB
#define KEY1_PIN           GPIO_Pin_6
#define KEY2_PORT          GPIOB
#define KEY2_PIN           GPIO_Pin_7

void GPIO_int(void);


#endif

