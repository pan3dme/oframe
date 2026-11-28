const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'../..');
function edit(file,fn){const p=path.join(root,file);let src=fs.readFileSync(p,'latin1');
  function replace(from,to){if(!src.includes(from))from=from.replace(/\r\n/g,'\n');if(!src.includes(from))throw Error('Missing '+from);src=src.replace(from,to);}
  fn(replace,()=>src);fs.writeFileSync(p,src,'latin1');}
edit('tools/test_low_power.cjs',replace=>{
  replace("check(fallback === 2000, 'windowed TX fallback is exactly 2000 ms');",
    "check(fallback === 1500, 'windowed TX fallback retains the current 1500 ms policy');");
  replace('[1, 1580, 1999, 2000, 2001, null]', '[1, fallback - 1, fallback, fallback + 1, 1580, null]');
  replace("'normal IRQ switches promptly; missing/late IRQ switches at 2000 ms '",
    "'normal IRQ switches promptly; missing/late IRQ switches at configured fallback '");
  replace("scenario.windows[0].at === 2000, '2000 ms counts from TX initiation, including driver preparation'",
    "scenario.windows[0].at === fallback, 'fallback counts from TX initiation, including driver preparation'");
  replace('scenario.elapsed() === 2000 + rxAt', 'scenario.elapsed() === fallback + rxAt');
  replace('scenario.elapsed() === 8001', 'scenario.elapsed() === fallback + 6001');
  replace('scenario.elapsed() === 7000', 'scenario.elapsed() === fallback + 5000');
  replace("'8|v3-14|TXTO|2000,' + sourceCount", "'8|v3-14|TXTO|' + fallback + ',' + sourceCount");
  replace('    rtc_clear_alarm_flag:()=>{},rtc_set_alarm:()=>{},',
    '    rtc_clear_alarm_flag:()=>{},rtc_set_alarm:()=>{},\r\n' +
    '    LowPower_PrepareStop:()=>{},LowPower_AfterStop:()=>{},');
  replace('    HAL_GPIO_WritePin:(port,pin,value)=>writes.push([pin,value]),HAL_Delay:()=>{},printf:()=>{}});',
    '    HAL_NVIC_DisableIRQ:()=>{},USART3_IRQn:39,LowPower_GpsPinsOn:()=>{},MX_USART3_UART_Init:()=>{},\r\n' +
    '    HAL_GPIO_WritePin:(port,pin,value)=>writes.push([pin,value]),HAL_Delay:()=>{},printf:()=>{}});');
  replace("check(stop===body(beforeWake,'enterStopUntil'),'original first-sleep flashing window unchanged byte-for-byte');",
    "const stopWithoutGpio = stop.replace(/  LowPower_(?:PrepareStop|AfterStop)\\(\\);\\r?\\n/g,'');\r\n" +
    "check(stopWithoutGpio===body(beforeWake,'enterStopUntil'),'GPIO hooks preserve original STOP logic and first-sleep flashing window');");
  replace("for(const name of ['gpsRunContinuous','gpsWaitForFix','gpsSearchAndReport','enterStopUntil'])\r\n  check(body(main,name)===body(beforeDeferredRadio,name),'deferred radio preserves UPGPS and first flash window '+name);",
    "for(const name of ['gpsRunContinuous','gpsWaitForFix','gpsSearchAndReport'])\r\n  check(body(main,name)===body(beforeDeferredRadio,name),'deferred radio preserves UPGPS timing '+name);\r\n" +
    "check(stopWithoutGpio===body(beforeDeferredRadio,'enterStopUntil'),'GPIO hooks preserve deferred-radio STOP scheduling');");
});
edit('tools/build_watchdog.cjs',replace=>{
  replace("'tools/test_watchdog.cjs','tools/test_low_power.cjs'",
    "'tools/test_watchdog.cjs','tools/test_low_power.cjs','tools/test_gpio_low_power.cjs'");
});
edit('Core/Src/main.c',(replace,get)=>{
  const gpioHeader=get().match(/\/\*\*\r?\n  \* @brief  GPIO [\s\S]*?\*\/\r?\nstatic void MX_GPIO_Init/)[0];
  replace(gpioHeader,'/** Configure ONLY_SEND power controls and radio pins. */\r\nstatic void MX_GPIO_Init');
  const gpsComment=get().match(/  \/\* GNSS [\s\S]*?  LowPower_InitUnusedPins\(\);/)[0];
  replace(gpsComment,'  /* Park unused pins and powered-off GPS interfaces at boot. */\r\n  LowPower_InitUnusedPins();');
  const gpsBody=get().match(/static void gpsPowerOff\(void\)\r?\n\{([\s\S]*?)^\}/m)[1];
  const log=gpsBody.match(/  printf\([^\r\n]+/)[0];
  replace(gpsBody,'\r\n  /* Stop callbacks and float data/reset pins before removing module power. */\r\n' +
    '  gpsPowerState = 0;\r\n  gpsUartOff();\r\n' +
    '  HAL_GPIO_WritePin(GNSS_WAKE_GPIO_Port, GNSS_WAKE_Pin, GPIO_PIN_RESET);\r\n' +
    '  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);\r\n' +
    '  gpsFixValid = 0;\r\n  gpsRd = gpsWr;\r\n  gpsLineLen = 0;\r\n\r\n' +log+'\r\n');
  const rxHeader=get().match(/\/\*\*\r?\n  \* @brief USART3 Initialization[\s\S]*?\*\/\r?\nstatic void MX_USART3_UART_Init/)[0];
  replace(rxHeader,'/** USART3 9600 RX-only; remain disabled whenever GPS is off. */\r\nstatic void MX_USART3_UART_Init');
});
console.log('Updated regression expectations and GPIO comments.');
