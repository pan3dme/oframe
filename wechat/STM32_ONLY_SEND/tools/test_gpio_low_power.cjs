/* Execute source-derived GPIO/UART lifecycles with modeled registers and HAL.
 * No MCU execution, electrical leakage model, or claim of measured current. */
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'latin1');
const low = read('Core/Src/low_power.c'), main = read('Core/Src/main.c');
const msp = read('Core/Src/stm32f1xx_hal_msp.c');
let passed = 0;
function check(value, message) { assert(value, message); passed++; }
function body(src, name) {
  const match = src.match(new RegExp('(?:static )?void ' + name + '\\([^)]*\\)\\r?\\n\\{([\\s\\S]*?)^\\}', 'm'));
  assert(match, 'Missing function: ' + name);
  return match[1];
}
function translate(src) {
  return src.replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/GPIO_InitTypeDef (\w+) = \{0\};/g, 'let $1 = {Pin:0,Mode:0,Pull:0,Speed:0};')
    .replace(/\((?:uint8_t|uint16_t|uint32_t)\s*\*\)/g, '')
    .replace(/\buint(?:8|16|32)_t\b/g, 'let')
    .replace(/&(?=(?:pin|GPIO_InitStruct|huart3|huart1|gpsByte)\b)/g, '')
    .replace(/->/g, '.').replace(/\b(0x[\da-f]+|\d+)u\b/gi, '$1')
    .replace(/CLEAR_BIT\(([^,]+),\s*([^;]+)\);/g, '$1 = ($1 & ~($2)) >>> 0;')
    .replace(/MODIFY_REG\(([^,]+),\s*([^,]+),\s*([^;]+)\);/g, '$1 = (($1 & ~($2)) | ($3)) >>> 0;');
}
function install(state, src, names) {
  for (const [name, args = ''] of names)
    vm.runInContext('function ' + name + '(' + args + '){' + translate(body(src, name)) + '\n}', state);
}
function environment(options = {}) {
  const events = [], pins = new Map();
  const port = name => ({name, CRL:0x44444444, CRH:0x44444444, ODR:0});
  const state = {GPIOA:port('A'),GPIOB:port('B'),GPIOC:port('C'),GPIOD:port('D'),
    EXTI:{IMR:0x20004,EMR:0x20000,RTSR:0x20004,FTSR:0},
    savedCrl:0,savedCrh:0,savedImr2:0,savedEmr2:0,
    GPIO_MODE_ANALOG:0,GPIO_MODE_INPUT:4,GPIO_MODE_OUTPUT_OD:6,
    GPIO_MODE_OUTPUT_PP:2,GPIO_MODE_AF_PP:11,GPIO_MODE_IT_RISING:8,
    GPIO_NOPULL:0,GPIO_PULLDOWN:2,GPIO_SPEED_FREQ_LOW:2,GPIO_SPEED_FREQ_HIGH:3,
    GPIO_PIN_SET:1,GPIO_PIN_RESET:0,HAL_OK:0,RESET_INIT:17,
    USART3_IRQn:39,uartClock:false,irq:false,armed:false,pending:false,uartEnabled:false,
    gpsPowerState:0,gpsRd:0,gpsWr:3,gpsLineLen:9,gpsRxCount:15,gpsFixValid:1,gpsByte:0,
    GPS_RING_LEN:512,gpsRing:new Array(512).fill(0),
    GPS_PWR_ON_DELAY_MS:1000,UART_WORDLENGTH_8B:0,UART_STOPBITS_1:0,UART_PARITY_NONE:0,
    UART_MODE_RX:4,UART_HWCONTROL_NONE:0,UART_OVERSAMPLING_16:0,
    huart3:{Instance:null,Init:{}}, USART3:{name:'UART3'},
    HAL_GPIO_Init:(p, cfg) => {
      for (let i = 0; i < 16; i++) if (cfg.Pin & (1 << i)) {
        const reg = i < 8 ? 'CRL' : 'CRH', shift = (i % 8) * 4;
        p[reg] = ((p[reg] & ~(15 << shift)) | (cfg.Mode << shift)) >>> 0;
        pins.set(p.name + i, {...cfg});
        events.push(['pin',p.name,i,cfg.Mode]);
      }
    },
    HAL_GPIO_WritePin:(p, pin, value) => {
      p.ODR = value ? p.ODR | pin : p.ODR & ~pin;
      events.push(['write',p.name,pin,value]);
      if (p === state.GPIOB && pin === 0x8000 && value === 1 && state.huart3.Instance && options.inspectCut)
        options.inspectCut(state, pins);
    },
    HAL_NVIC_DisableIRQ:()=>{state.irq=false;events.push(['mask']);},
    HAL_NVIC_EnableIRQ:()=>{check(state.armed && state.uartEnabled,'arm UART before enabling IRQ');state.irq=true;events.push(['unmask']);},
    HAL_NVIC_ClearPendingIRQ:()=>{state.pending=false;},HAL_NVIC_SetPriority:()=>{},
    __HAL_RCC_USART3_CLK_ENABLE:()=>{state.uartClock=true;events.push(['clock-on']);},
    __HAL_RCC_USART3_CLK_DISABLE:()=>{check(!state.uartEnabled && !state.armed,'disable UART and reception before gating clock');state.uartClock=false;events.push(['clock-off']);},
    HAL_UART_AbortReceive:()=>{
      check(state.uartClock && !state.irq,'abort with clock present and IRQ masked');
      state.armed=false;events.push(['abort']);return options.failAbort ? 1 : 0;
    },
    __HAL_UART_DISABLE:()=>{state.uartEnabled=false;},
    __HAL_UART_CLEAR_OREFLAG:()=>{check(state.uartClock,'clear UART flags before clock gating');events.push(['clear-flags']);},
    HAL_UART_Init:()=>{state.uartEnabled=true;events.push(['uart-init']);return options.failInit ? 1 : 0;},
    HAL_UART_Receive_IT:()=>{
      check(state.uartClock && state.uartEnabled && state.gpsPowerState===1,'receive only with active GPS/UART');
      if (options.failReceive) return 1;
      state.armed=true;events.push(['receive']);return 0;
    },
    HAL_Delay:()=>{check(state.armed && state.irq,'collect GPS boot bytes during power-on delay');},
    Safety_Reset:reason=>{throw Error('reset:'+reason);},Error_Handler:()=>{throw Error('reset:17');},
    gpsBatteryOk:()=>options.battery !== 29,printf:()=>{}}
  for (let i=0;i<16;i++) state['GPIO_PIN_'+i]=1<<i;
  Object.assign(state,{GPS_RST_Pin:8,GPS_RST_GPIO_Port:state.GPIOA,
    GNSS_WAKE_Pin:2,GNSS_WAKE_GPIO_Port:state.GPIOB,GPS_PWR_Pin:0x8000,GPS_PWR_GPIO_Port:state.GPIOB,
    LORA_SCK_Pin:32,LORA_MISO_Pin:64,LORA_MOSI_Pin:128,LORA_MISO_GPIO_Port:state.GPIOA,
    LORA_CSN_Pin:1,LORA_CSN_GPIO_Port:state.GPIOB,LORA_RST_Pin:2,LORA_RST_GPIO_Port:state.GPIOA});
  const ctx = vm.createContext(state);
  const flat = low.replace(/\\\r?\n/g,' ');
  for (const name of ['UNUSED_A','UNUSED_B','STOP_CRL_MASK','STOP_CRH_MASK']) {
    const definition = flat.match(new RegExp('^#define '+name+' (.*)$','m'))[1];
    state[name] = vm.runInContext(translate(definition),ctx);
  }
  install(ctx,low,['LowPower_InitUnusedPins','LowPower_GpsPinsOn','LowPower_GpsPinsOff','LowPower_PrepareStop','LowPower_AfterStop'].map(n=>[n]));
  install(ctx,main,['gpsUartOff','gpsPowerOn','gpsPowerOff','MX_USART3_UART_Init','MX_GPIO_Init'].map(n=>[n]));
  install(ctx,main,[['HAL_UART_RxCpltCallback','huart'],['HAL_UART_ErrorCallback','huart']]);
  for (const name of ['GPIOA','GPIOB','GPIOC','GPIOD']) state['__HAL_RCC_'+name+'_CLK_ENABLE']=()=>{};
  return {state,ctx,events,pins,run:code=>vm.runInContext(code,ctx)};
}
function nibble(p, i) { return ((i<8?p.CRL:p.CRH) >>> ((i%8)*4)) & 15; }
function offState(e) {
  const s=e.state;
  check(!s.irq && !s.armed && !s.uartClock && !s.uartEnabled,'GPS off has no UART clock, receiver or IRQ');
  check(nibble(s.GPIOA,3)===0 && nibble(s.GPIOB,10)===0 && nibble(s.GPIOB,11)===0,'GPS off leaves all data/reset pins analog');
  check((s.GPIOB.ODR&0x8002)===0x8000,'GPS off keeps high-side switch off and WAKEUP low');
}

// Boot config preserves crystal, SWD and boot pins while parking unused GPIO.
{
  const e=environment();e.run('MX_GPIO_Init(); MX_USART3_UART_Init();');offState(e);
  for (const i of [4,8,11,12,15]) check(nibble(e.state.GPIOA,i)===0,'unused PA'+i+' analog');
  for (const i of [3,4,5,6,7,8,9,12,13,14]) check(nibble(e.state.GPIOB,i)===0,'unused PB'+i+' analog');
  for (const [p,i] of [['GPIOA',13],['GPIOA',14],['GPIOB',2],['GPIOC',14],['GPIOC',15],['GPIOD',0],['GPIOD',1]])
    check(nibble(e.state[p],i)===4,'do not reconfigure reserved '+p+i);
  check(nibble(e.state.GPIOB,1)===6 && !(e.state.GPIOB.ODR&2),'WAKEUP starts open-drain low');
  check(nibble(e.state.GPIOA,6)===4,'SPI MISO is input at boot');
}

// Repeat off/on, and the clock-restoration reinitialization in both states.
{
  const e=environment({inspectCut:(s)=>{
    check(!s.armed && !s.irq && nibble(s.GPIOA,3)===0 && nibble(s.GPIOB,11)===0,'release reset/data before cutting module power');
    check(!(s.GPIOB.ODR&2),'pull WAKEUP down before cutting module power');
  }});
  e.run('MX_GPIO_Init(); MX_USART3_UART_Init();');offState(e);
  for(let i=0;i<5;i++) {
    e.run('gpsPowerOn();');
    check(e.state.gpsPowerState===1 && e.state.armed && e.state.irq,'GPS power-on restores receive path');
    check(nibble(e.state.GPIOA,3)===6 && (e.state.GPIOA.ODR&8),'RESET_N is released open-drain');
    check(nibble(e.state.GPIOB,10)===0 && nibble(e.state.GPIOB,11)===4,'only GPS TXD is connected to MCU receiver');
    const wr=e.state.gpsWr;
    e.state.gpsByte=0x42;e.state.armed=false;
    e.run('HAL_UART_RxCpltCallback(huart3);');
    check(e.state.gpsRing[wr]===0x42 && e.state.gpsWr===(wr+1)%512 && e.state.armed,'active RX stores bytes and rearms');
    e.state.armed=false;e.run('HAL_UART_ErrorCallback(huart3);');
    check(e.state.armed,'active UART error recovers reception');
    e.run('MX_USART3_UART_Init();');
    check(e.state.irq && e.state.armed,'active clock restore rearms receive');
    e.run('gpsPowerOff(); gpsPowerOff();');offState(e);
    e.run('MX_USART3_UART_Init();');offState(e);
    e.events.length=0;
    e.run('HAL_UART_RxCpltCallback(huart3); HAL_UART_ErrorCallback(huart3);');
    check(e.events.length===0,'stale RX/error callbacks after shutdown cannot restart UART');
  }
  check(!e.events.some(v=>v[0]==='pin' && v[1]==='A' && v[2]===3 && v[3]===2),'RESET_N is never push-pull high');
}
{
  const e=environment({battery:29});e.run('MX_GPIO_Init(); MX_USART3_UART_Init();');e.events.length=0;
  e.run('gpsPowerOn();');offState(e);check(e.events.length===0,'low battery cannot release pins or energize GPS');
}
for(const option of ['failAbort','failInit','failReceive']) {
  const e=environment({[option]:true});
  assert.throws(()=>e.run(option==='failAbort'?'MX_USART3_UART_Init();':'gpsPowerOn();'),/reset:17/);passed++;
  check(!e.state.irq,'UART failure cannot enable receive IRQ '+option);
}

// Arbitrary preexisting GPIO/EXTI configurations must survive full STOP cycles.
let seed=0x12345678;
function random(){seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed;}
{
  const e=environment(),s=e.state;
  for(let i=0;i<256;i++) {
    s.GPIOA.CRL=random();s.GPIOA.CRH=random();s.GPIOA.ODR=random()&0xffff;
    s.GPIOB.CRL=random();s.GPIOB.CRH=random();s.GPIOB.ODR=random()&0xffff;
    s.EXTI.IMR=random();s.EXTI.EMR=random();s.EXTI.RTSR=random();s.EXTI.FTSR=random();
    const before=JSON.stringify([s.GPIOA,s.GPIOB,s.EXTI]);
    e.run('LowPower_PrepareStop();');
    for(const pin of [2,6,9,10])check(nibble(s.GPIOA,pin)===0,'STOP parks PA'+pin);
    check(!(s.EXTI.IMR&4) && !(s.EXTI.EMR&4),'STOP masks radio IRQ/event while keeping RTC17');
    e.run('LowPower_AfterStop();');
    check(JSON.stringify([s.GPIOA,s.GPIOB,s.EXTI])===before,'restore exact GPIO, ODR, EXTI masks/edges');
  }
}
const stop=body(main,'enterStopUntil');
check(stop.indexOf('SysTick->CTRL = 0u;') < stop.indexOf('LowPower_PrepareStop();'),'park pins after disabling tick');
check(stop.indexOf('LowPower_PrepareStop();') < stop.indexOf('while ((int32_t)(wakeRtc - rtcNow) > 0)'),'park once before watchdog segments');
check(stop.indexOf('LowPower_AfterStop();') > stop.indexOf('Safety_AfterStop();'),'restore pins only after full sleep interval');
check(stop.indexOf('LowPower_AfterStop();') < stop.indexOf('if (savedRadioIrq)'),'restore GPIO before enabling radio IRQ');
const old=read('backups/gpio_low_power_20261128/Core/Src/main.c');
check(stop.replace(/  LowPower_(?:PrepareStop|AfterStop)\(\);\r?\n/g,'')===body(old,'enterStopUntil'),'all existing STOP scheduling/watchdog/download-window logic preserved');
for(const name of ['HAL_UART_RxCpltCallback','HAL_UART_ErrorCallback'])
  check(body(main,name).includes('huart->Instance == USART3 && gpsPowerState'),'ignore stale IRQ after GPS off '+name);
check(main.includes('huart1.Init.Mode = UART_MODE_TX;') && msp.includes('GPIO_MODE_ANALOG; /* Debug UART is TX-only. */'),'debug UART leaves unused RX analog');
check(/GPIO_InitStruct.Pin = GPIO_PIN_6;\s*GPIO_InitStruct.Mode = GPIO_MODE_INPUT;/.test(msp),'SPI MSP never restores MISO as output');
check(read('MDK-ARM/STM32_ONLY_SEND.uvprojx').includes('../Core/Src/low_power.c'),'Keil project includes low-power implementation');
const summary={passed,scope:'source-derived GPIO/register and GPS UART lifecycle models; no MCU execution or current measurement'};
fs.writeFileSync(path.join(root,'MDK-ARM/watchdog_build/test_gpio_low_power_results.json'),JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary,null,2));
