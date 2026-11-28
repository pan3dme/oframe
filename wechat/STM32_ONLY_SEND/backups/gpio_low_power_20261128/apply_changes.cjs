/* Preserve the original source bytes, including legacy comment encoding. */
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '../..');
function edit(file, fn) {
  const target = path.join(root, file);
  let src = fs.readFileSync(target, 'latin1');
  function replace(from, to) {
    if (!src.includes(from)) throw Error('Missing anchor in ' + file + ': ' + from);
    src = src.replace(from, to);
  }
  fn(replace, () => src);
  fs.writeFileSync(target, src, 'latin1');
}
edit('Core/Src/main.c', (replace, get) => {
  replace('#include "nvstore.h"', '#include "nvstore.h"\r\n#include "low_power.h"');
  replace('#define DEMO_VER     "V2.12"', '#define DEMO_VER     "V2.13"');
  replace('#define APP_VERSION         23u', '#define APP_VERSION         24u');
  replace('static uint8_t      gpsPowerState = 0;', 'static volatile uint8_t gpsPowerState = 0;');
  replace('static void gpsPowerOff(void);', 'static void gpsUartOff(void);\r\nstatic void gpsPowerOff(void);');
  replace('  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;\r\n  while',
    '  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;\r\n  LowPower_PrepareStop();\r\n  while');
  replace('  SystemCoreClockUpdate(); /* STOP wakes on HSI;',
    '  LowPower_AfterStop();\r\n  SystemCoreClockUpdate(); /* STOP wakes on HSI;');
  replace('    HAL_NVIC_DisableIRQ(USART3_IRQn);\r\n    if (HAL_UART_AbortReceive(&huart3) != HAL_OK) Safety_Reset(RESET_INIT);\r\n    MX_USART3_UART_Init();',
    '    MX_USART3_UART_Init(); /* Preserves GPS off / active state. */');
  replace('huart1.Init.Mode = UART_MODE_TX_RX;', 'huart1.Init.Mode = UART_MODE_TX;');
  replace('static void MX_USART3_UART_Init(void)\r\n{',
    'static void MX_USART3_UART_Init(void)\r\n{\r\n  HAL_NVIC_DisableIRQ(USART3_IRQn);\r\n  __HAL_RCC_USART3_CLK_ENABLE();\r\n  if (huart3.Instance == USART3 &&\r\n      HAL_UART_AbortReceive(&huart3) != HAL_OK) Safety_Reset(RESET_INIT);');
  const oldInitTail = get().match(/  HAL_NVIC_SetPriority\(USART3_IRQn, 2, 0\);[\s\S]*?  HAL_UART_Receive_IT\(&huart3, \(uint8_t \*\)&gpsByte, 1\);/)[0];
  replace(oldInitTail,
    '  HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);\r\n' +
    '  if (gpsPowerState) {\r\n' +
    '    LowPower_GpsPinsOn();\r\n' +
    '    __HAL_UART_CLEAR_OREFLAG(&huart3);\r\n' +
    '    if (HAL_UART_Receive_IT(&huart3, (uint8_t *)&gpsByte, 1) != HAL_OK)\r\n' +
    '      Safety_Reset(RESET_INIT);\r\n' +
    '    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);\r\n' +
    '    HAL_NVIC_EnableIRQ(USART3_IRQn);\r\n' +
    '  } else {\r\n' +
    '    gpsUartOff();\r\n' +
    '  }');
  const rstBlock = get().match(/  GPIO_InitStruct.Pin   = GPS_RST_Pin;[\s\S]*?  HAL_GPIO_WritePin\(GPS_RST_GPIO_Port, GPS_RST_Pin, GPIO_PIN_SET\);/)[0];
  replace(rstBlock,
    '  /* RESET_N stays analog while GPS is off; released open-drain on power-up. */\r\n' +
    '  LowPower_InitUnusedPins();');
  replace('  GPIO_InitStruct.Pin   = GNSS_WAKE_Pin;\r\n  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;',
    '  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET);\r\n' +
    '  GPIO_InitStruct.Pin   = GNSS_WAKE_Pin;\r\n' +
    '  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD; /* Module internal pull-up. */');
  replace('GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MISO_Pin | LORA_MOSI_Pin;',
    'GPIO_InitStruct.Pin  = LORA_SCK_Pin | LORA_MOSI_Pin;');
  replace('  /* USER CODE END MX_GPIO_Init_2 */',
    '  /* MISO is an input in SPI master mode. */\r\n' +
    '  GPIO_InitStruct.Pin = LORA_MISO_Pin;\r\n' +
    '  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;\r\n' +
    '  GPIO_InitStruct.Pull = GPIO_NOPULL;\r\n' +
    '  HAL_GPIO_Init(LORA_MISO_GPIO_Port, &GPIO_InitStruct);\r\n\r\n' +
    '  /* USER CODE END MX_GPIO_Init_2 */');
  replace('if (huart->Instance == USART3)\r\n  {',
    'if (huart->Instance == USART3 && gpsPowerState)\r\n  {');
  replace('if (huart->Instance == USART3)\r\n  {',
    'if (huart->Instance == USART3 && gpsPowerState)\r\n  {');
  replace('static void gpsPowerOn(void)\r\n{',
    '/* Idempotent shutdown: abort RX before gating the clock or floating pins. */\r\n' +
    'static void gpsUartOff(void)\r\n{\r\n' +
    '  HAL_NVIC_DisableIRQ(USART3_IRQn);\r\n' +
    '  __HAL_RCC_USART3_CLK_ENABLE();\r\n' +
    '  if (HAL_UART_AbortReceive(&huart3) != HAL_OK) Safety_Reset(RESET_INIT);\r\n' +
    '  __HAL_UART_DISABLE(&huart3);\r\n' +
    '  __HAL_UART_CLEAR_OREFLAG(&huart3);\r\n' +
    '  HAL_NVIC_ClearPendingIRQ(USART3_IRQn);\r\n' +
    '  __HAL_RCC_USART3_CLK_DISABLE();\r\n' +
    '  LowPower_GpsPinsOff();\r\n' +
    '}\r\n\r\n' +
    'static void gpsPowerOn(void)\r\n{');
  replace('  if (!gpsBatteryOk()) return;\r\n  gpsRd = gpsWr;',
    '  if (!gpsBatteryOk()) return;\r\n' +
    '  HAL_NVIC_DisableIRQ(USART3_IRQn);\r\n' +
    '  LowPower_GpsPinsOn();\r\n' +
    '  gpsRd = gpsWr;');
  replace('  HAL_Delay(GPS_PWR_ON_DELAY_MS);\r\n  gpsPowerState = 1;',
    '  gpsPowerState = 1;\r\n' +
    '  MX_USART3_UART_Init(); /* Arm RX before the module starts NMEA output. */\r\n' +
    '  HAL_Delay(GPS_PWR_ON_DELAY_MS);');
  replace('static void gpsPowerOff(void)\r\n{\r\n  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);',
    'static void gpsPowerOff(void)\r\n{\r\n' +
    '  gpsPowerState = 0;\r\n' +
    '  gpsUartOff();\r\n' +
    '  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET);\r\n' +
    '  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);');
});
edit('Core/Src/stm32f1xx_hal_msp.c', replace => {
  replace('GPIO_InitStruct.Pin = GPIO_PIN_10;\r\n    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;',
    'GPIO_InitStruct.Pin = GPIO_PIN_10;\r\n    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG; /* Debug UART is TX-only. */');
  replace('GPIO_InitStruct.Pin = GPIO_PIN_10;\r\n    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;',
    'GPIO_InitStruct.Pin = GPIO_PIN_10;\r\n    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG; /* GPS RXD is never driven. */');
  replace('GPIO_InitStruct.Pin   = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;',
    'GPIO_InitStruct.Pin   = GPIO_PIN_5 | GPIO_PIN_7;');
  replace('    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);\r\n  }\r\n}\r\n\r\n/* USER CODE END 1 */',
    '    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);\r\n' +
    '    GPIO_InitStruct.Pin = GPIO_PIN_6;\r\n' +
    '    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;\r\n' +
    '    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);\r\n' +
    '  }\r\n}\r\n\r\n/* USER CODE END 1 */');
});
edit('Core/Inc/main.h', (replace, get) => {
  const start = get().indexOf('/* --- GNSS ');
  const end = get().indexOf('/* ONLY_SEND ', start);
  if (start < 0 || end < 0) throw Error('Missing GPS pin definitions');
  replace(get().slice(start, end),
    '/* GNSS WAKEUP: open-drain, 0=standby, 1=release internal pull-up. */\r\n' +
    '#define GNSS_WAKE_Pin        GPIO_PIN_1\r\n' +
    '#define GNSS_WAKE_GPIO_Port  GPIOB\r\n\r\n' +
    '/* GPS high-side power switch: 0=on, 1=off. */\r\n' +
    '#define GPS_PWR_Pin          GPIO_PIN_15\r\n' +
    '#define GPS_PWR_GPIO_Port    GPIOB\r\n\r\n' +
    '/* L76K RESET_N: analog when off, released open-drain when powered.\r\n' +
    ' * No MCU-side pull-up; the module supplies its own internal pull-up. */\r\n' +
    '#define GPS_RST_Pin          GPIO_PIN_3\r\n' +
    '#define GPS_RST_GPIO_Port    GPIOA\r\n\r\n');
});
edit('MDK-ARM/STM32_ONLY_SEND.uvprojx', replace => {
  replace('              <FileName>nvstore.c</FileName>',
    '              <FileName>low_power.c</FileName>\r\n' +
    '              <FileType>1</FileType>\r\n' +
    '              <FilePath>../Core/Src/low_power.c</FilePath>\r\n' +
    '            </File>\r\n' +
    '            <File>\r\n' +
    '              <FileName>nvstore.c</FileName>');
});
console.log('GPIO low-power source changes applied.');
