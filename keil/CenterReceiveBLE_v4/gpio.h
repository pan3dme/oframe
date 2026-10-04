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

#define  RF_IRQ_PORT       GPIOA
#define  RF_IRQ_IO         GPIO_Pin_1

#define  CAD_PORT          GPIOA
#define  CAD_IO            GPIO_Pin_0

#define LED1_PORT          GPIOA
#define LED1_PIN           GPIO_Pin_15

//�װ��л�����ģʽѡ������ 
// Key pin definitions
#define KEY1_PIN        GPIO_Pin_0
#define KEY1_PORT       GPIOA

#define KEY2_PIN        GPIO_Pin_6
#define KEY2_PORT       GPIOB

#define KEY3_PIN        GPIO_Pin_7
#define KEY3_PORT       GPIOB

// RF mode select pin (reserved)
#define RF_MODE_PIN        GPIO_Pin_2
#define RF_MODE_PORT       GPIOA

void GPIO_int(void);


#endif

