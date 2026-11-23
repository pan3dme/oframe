/* Source-derived control-flow regressions. No MCU/peripheral emulation. */
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'latin1');
const safety = read('Core/Src/reliability.c'), main = read('Core/Src/main.c');
const radio = read('Radio/src/pan3029_rf.c');
let cases = 0;
function check(value, label) { assert(value, label); cases++; }
function body(src, name) {
  const match = src.match(new RegExp('(?:static )?(?:void|uint8_t|uint32_t|RF_Err_t) ' + name + '\\([^)]*\\)\\r?\\n\\{([\\s\\S]*?)^\\}', 'm'));
  assert(match, 'Missing function ' + name);
  return match[1];
}
function calls(src, name, replace) {
  const token = name + '(';
  for (let pos = 0; (pos = src.indexOf(token, pos)) >= 0;) {
    let end = pos + token.length, level = 1;
    for (; level && end < src.length; end++) {
      if (src[end] === '(') level++;
      else if (src[end] === ')') level--;
    }
    assert(!level, 'Unbalanced macro ' + name);
    const value = replace(src.slice(pos + token.length, end - 1));
    src = src.slice(0, pos) + value + src.slice(end);
    pos += value.length;
  }
  return src;
}
function macro(name, value) {
  const flat = radio.replace(/\\\r?\n/g, ' ');
  const match = flat.match(new RegExp('^#define ' + name + '\\([^)]*\\) (.*)$', 'm'));
  assert(match, 'Missing macro ' + name);
  return match[1].replace(/\b(?:value|fn)\b/g, value);
}
function js(src) {
  src = src.replace(/#if SPI_WRITE_CHECK[\s\S]*?#endif/g, '');
  src = calls(src, 'RF_BUS_BEGIN', () => macro('RF_BUS_BEGIN', ''));
  src = calls(src, 'RF_BUS_ASSERT', value => macro('RF_BUS_ASSERT', value));
  src = calls(src, 'RF_BUS_RETURN', value => macro('RF_BUS_RETURN', value));
  src = calls(src, 'RF_BUS_READ_RETURN', value => macro('RF_BUS_READ_RETURN', value));
  return src.replace(/\(int32_t\)\(([^()]+)\)/g, '(($1) | 0)')
    .replace(/\(uint32_t\)\(now - (rtcTick|phaseTick)\)/g, '((now - $1) >>> 0)')
    .replace(/\((?:uint8_t|uint32_t|uint64_t|RF_Err_t|int|unsigned long)\)/g, '')
    .replace(/\b(?:uint8_t|uint16_t|uint32_t|RF_Err_t|int)\b/g, 'let')
    .replace(/\bstatic let\b/g, 'let')
    .replace(/\b(0x[\da-f]+|\d+)[uU]\b/gi, '$1').replace(/->/g, '.')
    .replace(/\bdo\s*\{/g, 'do {');
}
function install(ctx, src, functions) {
  for (const [name, args] of functions)
    vm.runInContext('function ' + name + '(' + args + '){' + js(body(src, name)) + '\n}', ctx);
}

// A stopped RTC must reset even if MAIN is re-armed and fed every 100 ms.
let tick = 0, feeds = 0;
const env = { rtcSeen: 0, rtcValue: 0, rtcTick: 0, phaseTick: 0, phaseLimit: 0,
  feedTick: 0, currentStage: 0, BKP: {}, IWDG: {}, STAGE_MAIN: 4,
  RESET_RTC: 2, RESET_DEADLINE: 16, HAL_GetTick: () => tick,
  Safety_Reset: reason => { throw Error('reset:' + reason); } };
Object.defineProperty(env.IWDG, 'KR', { set: () => feeds++ });
const ctx = vm.createContext(env);
install(ctx, safety, [['Safety_CheckRtc', 'rtc'], ['Safety_Begin', 'stage,maxMs'], ['Safety_Feed', '']]);
for (tick = 0; tick < 3000; tick += 100)
  vm.runInContext('Safety_Begin(STAGE_MAIN,15000); Safety_CheckRtc(100); Safety_Feed();', ctx);
assert.throws(() => vm.runInContext('Safety_CheckRtc(100)', ctx), /reset:2/); cases++;
check(feeds === 29, 'MAIN kept feeding before RTC fault deadline');

// Advancing RTC (including wrap) and paused HAL tick during long STOP are valid.
env.rtcSeen = 0;
for (tick = 0; tick <= 60000; tick += 100)
  vm.runInContext('Safety_CheckRtc(' + ((0xFFFFFFF0 + Math.floor(tick / 1334)) >>> 0) + ')', ctx);
check(true, 'slow valid RTC and 32-bit RTC wrap');
for (let rtc = 100; rtc < 1000; rtc += 3) vm.runInContext('Safety_CheckRtc(' + rtc + ')', ctx);
check(true, 'STOP segments advance RTC while HAL tick is paused');
env.rtcSeen = 0; tick = 0xFFFFFF00;
vm.runInContext('Safety_CheckRtc(50)', ctx);
tick = (tick + 2999) >>> 0; vm.runInContext('Safety_CheckRtc(50)', ctx);
tick = (tick + 1) >>> 0;
assert.throws(() => vm.runInContext('Safety_CheckRtc(50)', ctx), /reset:2/); cases++;

// Try an IRQ at every low-level register operation of each real page function.
const functions = [['rf_switch_page','page'], ['rf_read_spec_page_reg','page,addr'],
  ['rf_write_spec_page_reg','page,addr,value'], ['rf_set_spec_page_reg_bits','page,addr,mask'],
  ['rf_reset_spec_page_reg_bits','page,addr,mask'], ['rf_write_spec_page_reg_bits','page,addr,val,mask']];
for (const [expression, expected] of [
  ['rf_write_spec_page_reg(3,0x26,15)',15],
  ['rf_set_spec_page_reg_bits(3,0x26,8)',10],
  ['rf_reset_spec_page_reg_bits(3,0x26,2)',0],
  ['rf_write_spec_page_reg_bits(3,0x26,2,6)',4],
  ['rf_read_spec_page_reg(3,0x26)',null]
]) for (let at = 1; at <= 16; at++) for (const initialMask of [0,1]) {
  let primask = initialMask, page = 0, ops = 0, pending = false, injected = false;
  const writes = [], data = new Map([['3:38',2]]);
  function dispatch() { if (!primask && pending) { pending = false; page = 0; } }
  function operation() { if (++ops === at) { pending = true; injected = true; dispatch(); } }
  const reg = { RF_OK: 0, RF_FAIL: 1, OK: 0, FAIL: 1, REG_SYS_CTL: 0,
    GET_SHIFT: value => { let bit=0; while (bit<8 && !(value & (1<<bit))) bit++; return bit===8?0:bit; },
    __get_PRIMASK: () => primask, __disable_irq: () => { primask = 1; },
    __set_PRIMASK: value => { primask = value; dispatch(); },
    rf_read_reg: addr => { const value = addr === 0 ? page : (data.get(page+':'+addr) || 0); operation(); return value; },
    rf_write_reg: (addr,value) => { if (!addr) page = value & 3;
      else { writes.push({page,addr,value}); data.set(page+':'+addr,value); }
      operation(); return 0; } };
  const rfctx = vm.createContext(reg); install(rfctx, radio, functions);
  const value = vm.runInContext(expression, rfctx);
  check(primask === initialMask, 'restore caller interrupt state');
  if (expected === null) check(value === 2, 'read completes before pending IRQ');
  else check(writes.length === 1 && writes[0].page === 3 && writes[0].value === expected, 'atomic page/read-modify-write');
  if (injected && !initialMask) check(!pending, 'pending IRQ runs after access');
}

// Checked preparation retries once; success retains an unchecked second pass.
for (const outcomes of [[0],[1,0],[1,1]]) {
  let primask = 0; const passes = [];
  const rfctx = vm.createContext({ RF_OK:0, RF_FAIL:1,
    __get_PRIMASK:()=>primask, __disable_irq:()=>{primask=1;}, __set_PRIMASK:v=>{primask=v;},
    rf_deepsleep_once: verify => { passes.push(verify); return verify ? outcomes.shift() : 0; } });
  install(rfctx, radio, [['rf_deepsleep','']]);
  const result = vm.runInContext('rf_deepsleep()', rfctx);
  check(primask === 0, 'sleep restores interrupt mask');
  check(result === (passes[passes.length-1] === 0 ? 0 : 1), 'sleep outcome propagated');
  check(passes.length <= 3 && passes[0] === 1, 'bounded verified sleep attempts');
}
for (const verify of [0,1]) for (const succeedsAt of [0,1,2,3]) {
  let writes = 0, reads = 0;
  const rfctx = vm.createContext({RF_OK:0,RF_FAIL:1,
    rf_write_reg:()=>{writes++;},rf_read_reg:()=>{reads++;return writes>succeedsAt?6:0;}});
  install(rfctx,radio,[['rf_write_reg_verified','addr,value,verify']]);
  const result=vm.runInContext('rf_write_reg_verified(4,6,'+verify+')',rfctx);
  check(result === (!verify || succeedsAt<3 ? 0 : 1),'readback exhaustion reported');
  check(writes<=3 && (verify || reads===0),'sleeping module not read during second pass');
}

// Repeated sleep requests do not communicate with an already sleeping radio.
for (const initialized of [0,1]) for (const result of [0,1]) {
  let calls=0;
  const sleepctx=vm.createContext({loraInitialized:initialized,RF_OK:0,RESET_RADIO:6,
    rf_deepsleep:()=>{calls++;return result;},Safety_Reset:()=>{throw Error('radio reset');}});
  install(sleepctx,main,[['loraSleep','']]);
  if(initialized && result) {assert.throws(()=>vm.runInContext('loraSleep()',sleepctx),/radio reset/);cases++;}
  else {vm.runInContext('loraSleep(); loraSleep();',sleepctx);check(calls===initialized,'sleep is idempotent');}
}
// In fault backoff, a failed radio is held in reset through the whole sleep.
const backoffStart=main.indexOf('    uint32_t backoff = Safety_BackoffSeconds();');
const backoffEnd=main.indexOf('\n  }',backoffStart);
assert(backoffStart>=0 && backoffEnd>backoffStart);
for(const initialized of [0,1])for(const sleepResult of [0,1]){
  let held=false,slept=0,sleepCalls=0;
  const backctx=vm.createContext({Safety_BackoffSeconds:()=>30,printf:()=>{},
    loraTryInit:()=>initialized,rf_deepsleep:()=>{sleepCalls++;return sleepResult;},
    RF_OK:0,loraInitialized:1,LORA_RST_GPIO_Port:0,LORA_RST_Pin:2,GPIO_PIN_RESET:0,
    HAL_GPIO_WritePin:(port,pin,value)=>{held=value===0;},
    getCurrentTimestamp:()=>100,enterStopUntil:target=>{slept=target;},SystemClock_Config:()=>{}});
  vm.runInContext(js(main.slice(backoffStart,backoffEnd)),backctx);
  check(slept===130 && backctx.loraInitialized===0,'fault backoff always reaches sleep');
  check(held===(!initialized||sleepResult!==0),'failed radio remains held in reset');
  check(sleepCalls===initialized,'no SPI sleep attempt after failed initialization');
}
const stop=body(main,'enterStopUntil');
// Execute the STOP loop with a modeled RTC event and hardware watchdog expiry.
// The WFE/analog hardware itself is not emulated or proven by this test.
for(const mode of ['normal','spurious','frozen','jump']) {
  let rtc=0,rawFeeds=0,sleeps=0,spur=0;
  const enabled=new Set([8,39]);
  const state={rtcSeen:0,rtcValue:0,rtcTick:0,phaseTick:0,phaseLimit:0,
    feedTick:0,currentStage:0,BKP:{},IWDG:{},STAGE_STOP:10,STAGE_MAIN:4,
    RESET_RTC:2,HAL_GetTick:()=>5,Safety_HadFault:()=>1,
    Safety_Reset:reason=>{throw Error('reset:'+reason);},
    timeSynced:0,WDG_SEG_SEC:3,SysTick:{CTRL:3},SCB:{SCR:0},EXTI:{},PWR:{CR:0},
    EXTI2_IRQn:8,USART3_IRQn:39,GPIO_PIN_2:4,
    EXTI_PR_PR17:1<<17,SCB_ICSR_PENDSTCLR_Msk:1<<25,SCB_SCR_SLEEPDEEP_Msk:4,
    PWR_CR_PDDS:2,PWR_CR_LPDS:1,PWR_CR_CWUF:4,TICK_INT_PRIORITY:15,
    SysTick_CTRL_TICKINT_Msk:2,HAL_OK:0,
    NVIC_GetEnableIRQ:irq=>enabled.has(irq)?1:0,
    HAL_NVIC_DisableIRQ:irq=>enabled.delete(irq),HAL_NVIC_EnableIRQ:irq=>enabled.add(irq),
    HAL_NVIC_ClearPendingIRQ:()=>{},__HAL_GPIO_EXTI_CLEAR_IT:()=>{},
    rtc_clear_alarm_flag:()=>{},rtc_set_alarm:()=>{},
    __SEV:()=>{},__DSB:()=>{},__ISB:()=>{},
    SystemCoreClockUpdate:()=>{},HAL_InitTick:()=>{state.SysTick.CTRL=3;return 0;},
    HAL_SuspendTick:()=>{state.SysTick.CTRL&=~2;},
    __WFE:()=>{if(state.SCB.SCR&4){sleeps++;
      if(mode==='frozen'){if(sleeps===10)throw Error('IWDG expiry');}
      else if(mode==='spurious' && spur++<3){} else rtc+=mode==='jump'?6:3;
    }}}
  Object.defineProperty(state.IWDG,'KR',{set:()=>rawFeeds++});
  const stopctx=vm.createContext(state);
  state.rtc_read_counter=()=>{vm.runInContext('Safety_CheckRtc('+rtc+')',stopctx);return rtc;};
  install(stopctx,safety,[['Safety_CheckRtc','rtc'],['Safety_Begin','stage,maxMs'],
    ['Safety_PrepareStop',''],['Safety_AfterStop','']]);
  const translated=js(stop).replace('MODIFY_REG(PWR.CR, PWR_CR_PDDS | PWR_CR_LPDS, PWR_CR_LPDS | PWR_CR_CWUF)',
    'PWR.CR = (PWR.CR & ~(PWR_CR_PDDS | PWR_CR_LPDS)) | PWR_CR_LPDS | PWR_CR_CWUF');
  vm.runInContext('function enterStopUntil(wakeTs){'+translated+'}',stopctx);
  if(mode==='frozen'){
    assert.throws(()=>vm.runInContext('enterStopUntil(9)',stopctx),/IWDG expiry/);cases++;
    check(rawFeeds===1,'frozen RTC/spurious wakes cannot keep feeding');
  } else if(mode==='jump'){
    assert.throws(()=>vm.runInContext('enterStopUntil(9)',stopctx),/reset:2/);cases++;
    check(rawFeeds===1,'RTC overrun resets before post-STOP feed');
  } else {
    vm.runInContext('enterStopUntil(9)',stopctx);
    check(rtc===9 && rawFeeds===6,'three successful RTC segments feed at boundaries');
    check(enabled.has(8)&&enabled.has(39)&&state.SysTick.CTRL===3,'restore IRQs and tick after STOP');
    if(mode==='spurious')check(sleeps===6,'spurious wakes add no feeds');
  }
}
check(stop.includes('NVIC_GetEnableIRQ(EXTI2_IRQn)') && stop.includes('NVIC_GetEnableIRQ(USART3_IRQn)'), 'save both IRQ enables');
check(stop.includes('if (savedRadioIrq) HAL_NVIC_EnableIRQ(EXTI2_IRQn)') && stop.includes('if (savedGpsIrq) HAL_NVIC_EnableIRQ(USART3_IRQn)'), 'restore both IRQ enables');
check(stop.indexOf('if ((int32_t)(after - target) >= 0) break;') < stop.indexOf('Safety_AfterStop();'), 'RTC deadline precedes STOP feed');
check(!stop.includes('loraInitFirstTime()'), 'segment wake does not reinitialize radio');
check(read('MDK-ARM/startup_stm32f103xb.s').includes('DCD     RTCAlarm_IRQHandler') && read('Core/Src/stm32f1xx_it.c').includes('void RTCAlarm_IRQHandler(void)'), 'RTC vector resolves');
check(!body(main,'checkVersionReportFlag').includes('Safety_MarkLinkHealthy'), 'version changes cannot erase a failure streak');
// All boots use the same grace; strictly more than 100 frames enables skipping.
const grace=Number(main.match(/#define BATTERY_SKIP_GRACE_TX (\d+)u/)[1]);
check(grace===100,'100-frame boot grace');
const batteryCondition=main.match(/if \((cachedBatteryPercent < BATTERY_SKIP_PCT &&\s*bootTxRounds > BATTERY_SKIP_GRACE_TX)\)/);
assert(batteryCondition,'missing low-battery decision');
const shouldSkip=Function('cachedBatteryPercent','bootTxRounds','BATTERY_SKIP_PCT','BATTERY_SKIP_GRACE_TX','return '+batteryCondition[1]+';');
for(const count of [0,99,100,101])for(const battery of [0,29,30,99])
  check(shouldSkip(battery,count,30,grace)===(count>100&&battery<30),'battery boundary '+count+'/'+battery);
const countctx=vm.createContext({tx_count:999,TX_COUNT_MAX:999,bootTxRounds:100,WINDOW_PRINT_MAX_ROUNDS:10,windowPrintOff:0});
install(countctx,main,[['txCountNext','']]);
vm.runInContext('txCountNext()',countctx);
check(countctx.bootTxRounds===101 && countctx.tx_count===1,'boot count survives wire counter wrap');
const summary={passed:cases,scope:'source-derived RTC/IRQ/sleep decision regressions; no board execution'};
fs.writeFileSync(path.join(root,'MDK-ARM/watchdog_build/test_low_power_results.json'),JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary,null,2));
