/* Source-derived control-flow regressions. No MCU/peripheral emulation. */
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'latin1');
const safety = read('Core/Src/reliability.c'), main = read('Core/Src/main.c');
const radio = read('Radio/src/pan3029_rf.c');
let cases = 0;
function check(value, label) { assert(value, label); cases++; }
function body(src, name) {
  const match = src.match(new RegExp('(?:static )?(?:void|uint8_t|uint32_t|RF_Err_t) ' + name + '\\([^)]*\\)\\r?\\n[ \\t]*\\{([\\s\\S]*?)^\\}', 'm'));
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
    .replace(/\((?:void|uint8_t|uint16_t|uint32_t|uint64_t|RF_Err_t|int|long|unsigned int|unsigned long)\)/g, '')
    .replace(/\b(?:uint8_t|uint16_t|uint32_t|int32_t|RF_Err_t|int)\b/g, 'let')
    .replace(/\bstatic let\b/g, 'let')
    .replace(/\b(0x[\da-f]+|\d+)[uU]\b/gi, '$1').replace(/->/g, '.')
    .replace(/\bdo\s*\{/g, 'do {');
}
function install(ctx, src, functions) {
  for (const [name, args] of functions)
    vm.runInContext('function ' + name + '(' + args + '){' + js(body(src, name)) + '\n}', ctx);
}

// Execute the actual TX wait and RX-window bodies with timed IRQ injection.
// This checks software decisions, not RF timing or physical register behavior.
{
  const fallback = Number(main.match(/#define TX_RX_FALLBACK_MS (\d+)u/)[1]);
  const cap = Number(main.match(/#define LORA_TX_MAX\s+(\d+)/)[1]);
  check(fallback === 2000, 'windowed TX fallback is exactly 2000 ms');
  check(/#define RX_WINDOW_MS 5000u/.test(main) &&
    /#define RX_IRQ_GUARD_MS \(RX_WINDOW_MS \+ 1000u\)/.test(main) &&
    /#define RX_STAGE_LIMIT_MS \(RX_WINDOW_MS \+ 2000u\)/.test(main),
    '5-second RX budget has a 6-second IRQ guard and 7-second watchdog phase');
  const txBody = js(body(main, 'loraTx')
    .replace(/\(uint32_t\)\(HAL_GetTick\(\) - start\)/g, '((HAL_GetTick() - start) >>> 0)')
    .replace(/\(uint8_t \*\)/g, '').replace(/&tx_time/g, 'null'));
  const rxBody = js(body(main, 'openRxWindow5000')
    .replace(/HAL_GetTick\(\) - rxStart/g, '((HAL_GetTick() - rxStart) >>> 0)')
    .replace('uint8_t local_buf[RX_LEN];', 'let local_buf = new Array(RX_LEN);')
    .replace(/\(char \*\)/g, ''));
  function txScenario(options = {}) {
    let elapsed = 0, txFlag = 9, rxFlag = 0, irqEnabled = true;
    let hardwareRx = false, pendingRx = false, pendingTx = true, deadline = 0;
    const events = [], windows = [], logs = [], parsed = [];
    const tick = () => ((options.base || 0) + elapsed) >>> 0;
    const state = {
      LORA_TX_MAX: cap, TX_RX_FALLBACK_MS: fallback, RX_WINDOW_MS: 5000,
      RX_IRQ_GUARD_MS: 6000, RX_STAGE_LIMIT_MS: 7000, RADIO_FLAG_IDLE: 0,
      RADIO_FLAG_RXDONE: 2, RADIO_FLAG_RXERR: 4, RF_MODE_STB3: 4, RF_OK: 0,
      RESET_TX: 'TX', RESET_RX: 'RX', STAGE_TX: 1, STAGE_RX: 2, STAGE_MAIN: 3,
      EXTI2_IRQn: 8, GPIO_PIN_2: 4, rxWindowStart: 0, rxWindowArmed: 0,
      tx_count: 4, txTimeoutNeedReport: 0, txTimeoutSourceCount: 0,
      RX_LEN: 64, rxWinCount: 0, rxOkCount: 0, windowPrintOff: 1, bootTxRounds: 1,
      lastRxRssi: 0, lastRxSnr: 0, lastRxSeen: 0,
      RxDoneParams: { Rssi: 180, Snr: 7, Size: 3, Payload: [52, 124, 120] },
      HAL_GetTick: tick, strlen: value => value.length,
      Safety_Begin: stage => events.push(['stage', stage]),
      Safety_Reset: reason => { throw Error('reset:' + reason); },
      rf_set_transmit_flag: flag => { txFlag = flag; }, rf_get_transmit_flag: () => txFlag,
      rf_set_recv_flag: flag => { rxFlag = flag; }, rf_get_recv_flag: () => rxFlag,
      rf_single_tx_data: (msg, bytes) => {
        events.push(['send', elapsed, bytes]);
        elapsed += options.prepareMs || 0;
        return options.failSend ? 1 : 0;
      },
      wdg_feed: () => {
        elapsed++;
        if (!hardwareRx && options.doneAt != null && elapsed >= options.doneAt && irqEnabled)
          txFlag = 1;
        if (hardwareRx && elapsed >= deadline && options.receive !== 'lost') rxFlag = 3;
        if (hardwareRx && options.rxAt != null && elapsed >= windows[0].at + options.rxAt && elapsed <= deadline)
          rxFlag = 2;
        if (elapsed > 10000) throw Error('unbounded wait');
      },
      HAL_NVIC_DisableIRQ: () => { irqEnabled = false; events.push(['disable']); },
      rf_set_mode: mode => {
        check(!irqEnabled && mode === 4, 'retire TX in STB3 with EXTI masked');
        events.push(['standby']); return options.failStandby ? 1 : 0;
      },
      rf_set_ldo_pa_off: () => {
        check(!irqEnabled, 'PA cleanup excludes late TX ISR');
        events.push(['pa-off']); return options.failPa ? 1 : 0;
      },
      rf_clr_irq: mask => {
        check(!irqEnabled && mask === 0x7F, 'clear stale radio IRQs before RX');
        pendingTx = false; events.push(['clear-radio']);
      },
      __HAL_GPIO_EXTI_CLEAR_IT: () => events.push(['clear-exti']),
      HAL_NVIC_ClearPendingIRQ: () => {
        check(!hardwareRx, 'clear pending EXTI before RX can produce a frame');
        events.push(['clear-nvic']);
      },
      rf_enter_single_timeout_rx: timeout => {
        windows.push({ at: elapsed, timeout }); events.push(['rx']);
        if (options.failRx) return 1;
        hardwareRx = true; deadline = elapsed + timeout;
        if (options.receive === 'immediate' || (options.receive === 'crcThenOk' && windows.length > 1)) {
          if (irqEnabled) rxFlag = 2; else pendingRx = true;
        }
        return 0;
      },
      HAL_NVIC_EnableIRQ: () => {
        check(state.rxWindowArmed === 1, 'publish RX handoff before unmasking EXTI');
        irqEnabled = true; events.push(['enable']);
        if (pendingTx) throw Error('stale TX IRQ survived cleanup');
        if (pendingRx) { rxFlag = 2; pendingRx = false; }
        if (options.receive === 'crcThenOk') rxFlag = 4;
      },
      printf: (format, ...args) => {
        if (format.startsWith('[TX')) {
          if (options.withRx !== 0) check(hardwareRx, 'RX starts before TX log blocks');
          logs.push(args[0]); elapsed += 7;
        }
      },
      __disable_irq: () => {}, __enable_irq: () => {},
      parse_rx_message: (payload, size) => parsed.push(payload.slice(0, size))
    };
    const context = vm.createContext(state);
    vm.runInContext('function loraTx(msg,withRx){' + txBody + '}\n' +
      'function openRxWindow5000(){' + rxBody + '}', context);
    return { state, context, events, windows, logs, parsed,
      run: () => vm.runInContext('loraTx("' + 'x'.repeat(options.length || 32) + '",' +
        (options.withRx === 0 ? 0 : 1) + ')', context, { timeout: 1000 }),
      receive: () => vm.runInContext('openRxWindow5000()', context, { timeout: 1000 }),
      elapsed: () => elapsed, advance: ms => { elapsed += ms; } };
  }
  for (const base of [0, 0xFFFFFF00]) for (const doneAt of [1, 1580, 1999, 2000, 2001, null]) {
    const scenario = txScenario({ base, doneAt, receive: 'immediate' });
    scenario.run();
    const expected = doneAt != null && doneAt <= fallback ? doneAt : fallback;
    check(scenario.windows.length === 1 && scenario.windows[0].at === expected,
      'normal IRQ switches promptly; missing/late IRQ switches at 2000 ms ' + base + '/' + doneAt);
    check(scenario.windows[0].timeout === 5000 && scenario.state.rxWindowStart === ((base + expected) >>> 0),
      'fresh 5-second budget begins at actual RX handoff');
    check(scenario.logs[0] === (doneAt == null || doneAt > fallback ? ' timeout RX' : ''),
      'fallback has distinct log and is not labelled TX success');
    const forced = doneAt == null || doneAt > fallback;
    check(scenario.state.txTimeoutNeedReport === (forced ? 1 : 0) &&
      scenario.state.txTimeoutSourceCount === (forced ? 4 : 0),
      'only forced RX records a pending diagnostic and affected uplink sequence');
    scenario.advance(15); scenario.receive();
    check(scenario.windows.length === 1 && scenario.parsed.length === 1,
      'preserve packet received during TX logging/GPS cleanup without rearming');
    check(scenario.state.rxWinCount === 1 && scenario.state.rxOkCount === 1 && scenario.state.rxWindowArmed === 0,
      'handoff consumed once and RX statistics remain correct');
    check(scenario.state.txTimeoutNeedReport === (forced ? 1 : 0),
      'successful downlink does not erase a TX timeout diagnostic');
    check(JSON.stringify(scenario.parsed[0]) === '[52,124,120]', 'early payload retained');
  }
  {
    const scenario = txScenario({ prepareMs: 20, receive: 'immediate' }); scenario.run();
    check(scenario.windows[0].at === 2000, '2000 ms counts from TX initiation, including driver preparation');
  }
  for (const rxAt of [4500, 4999]) {
    const scenario = txScenario({ rxAt }); scenario.run(); scenario.receive();
    check(scenario.parsed.length === 1 && scenario.elapsed() === 2000 + rxAt,
      'downlink after the former 4-second boundary is accepted at ' + rxAt);
  }
  for (const receive of ['timeout', 'crcThenOk', 'lost']) {
    const scenario = txScenario({ receive, base: 0xFFFFFF00 }); scenario.run(); scenario.advance(100);
    if (receive === 'lost') {
      assert.throws(scenario.receive, /reset:RX/); cases++;
      check(scenario.elapsed() === 8001, 'lost RX IRQ resets after the 6-second software guard');
    } else {
      scenario.receive();
      check(receive === 'timeout' ? scenario.elapsed() === 7000 : scenario.parsed.length === 1,
        'RX timeout/CRC retry respects window handoff ' + receive);
      check(scenario.state.txTimeoutNeedReport === 1 && scenario.state.txTimeoutSourceCount === 4,
        'pending diagnostic survives RX timeout and CRC retry');
      if (receive === 'crcThenOk') check(scenario.windows[1].timeout === 4893,
        'CRC rearm uses remaining original budget, including log and cleanup time');
    }
  }
  for (const doneAt of [1580, 2500, null]) {
    const scenario = txScenario({ withRx: 0, doneAt });
    if (doneAt == null) { assert.throws(scenario.run, /reset:TX/); cases++; }
    else scenario.run();
    check(scenario.windows.length === 0, 'auxiliary/UPGPS TX never opens RX');
    check(scenario.state.txTimeoutNeedReport === 0, 'non-windowed TX cannot queue timeout diagnostic');
    check(scenario.elapsed() === (doneAt == null ? 5000 : doneAt + 7),
      'non-windowed TX keeps normal IRQ wait and 5-second reset');
  }
  for (const failure of ['failSend', 'failStandby', 'failPa', 'failRx']) {
    const scenario = txScenario({ [failure]: true });
    assert.throws(scenario.run, failure === 'failSend' ? /reset:TX/ : /reset:RX/); cases++;
    check(scenario.logs.length === 0, 'failed radio access cannot claim RX is listening ' + failure);
  }
  {
    const scenario = txScenario({ length: 80, receive: 'immediate' }); scenario.run();
    check(scenario.events.find(event => event[0] === 'send')[2] === cap, 'existing on-air cap retained');
  }
  check(body(main, 'OnMaster').includes('loraTx(tx_msg, 1u);'), 'periodic time report enables RX fallback');
  check(body(main, 'sendGpsPositionMessage').includes('loraTx(tx_msg, useGpsType);'),
    'periodic GPS enables fallback while UPGPS retains no-window policy');
  for (const name of ['sendCenterMessage', 'sendConfigMessage', 'sendResetInfoMessage', 'sendReadInfoMessage', 'sendTxTimeoutWarning'])
    check(body(main, name).includes('loraTx(tx_msg, 0u);'), 'auxiliary TX unchanged ' + name);

  // Run the actual pending-report dispatcher and warning frame builder.
  for (const counted of [false, true]) for (const sourceCount of [1, 4, 999]) {
    let elapsed = 0, sent = [], failWarning = false;
    const warning = {
      TX_LEN: 128, MSG_TYPE_READINFO: 8, TX_RX_FALLBACK_MS: fallback,
      deviceName: 'v3-14', tx_count: sourceCount, txTimeoutSourceCount: sourceCount,
      txTimeoutNeedReport: 1, bootRstPending: 0, configNeedReport: 0,
      readInfoNeedReport: 0, centerNeedReport: 0,
      HAL_Delay: ms => { elapsed += ms; }, sizeof: () => 128,
      txCountNext: () => { warning.tx_count = warning.tx_count === 999 ? 1 : warning.tx_count + 1; },
      snprintf: (buffer, size, format, ...args) => {
        let i = 0; buffer[0] = format.replace(/%(?:lu|d|s)/g, () => String(args[i++]));
      },
      loraTx: (buffer, withRx) => {
        check(warning.txTimeoutNeedReport === 1, 'diagnostic remains pending through transmit');
        check(withRx === 0 && elapsed >= 1000, 'warning waits 1 second and never opens RX');
        if (failWarning) throw Error('warning TX reset');
        sent.push(buffer[0]);
      },
      sendResetInfoMessage: () => {}, sendConfigMessage: () => {},
      sendReadInfoMessage: () => {}, sendCenterMessage: () => {}
    };
    const warningContext = vm.createContext(warning);
    const warningBody = body(main, 'sendTxTimeoutWarning')
      .replace(/#if TX_COUNT_ENABLE\s*([\s\S]*?)#else\s*([\s\S]*?)#endif/g,
        (_, yes, no) => counted ? yes : no)
      .replace(/\bchar\s+(\w+)\[[^\]]+\]/g, 'let $1=[]');
    vm.runInContext('function sendTxTimeoutWarning(){' + js(warningBody) + '}\n' +
      'function reportPendingMessages(){' + js(body(main, 'reportPendingMessages')) + '}', warningContext);
    vm.runInContext('reportPendingMessages()', warningContext);
    const reportCount = sourceCount === 999 ? 1 : sourceCount + 1;
    const expected = '8|v3-14|TXTO|2000,' + sourceCount + '|' + (counted ? reportCount + '|' : '');
    check(sent.length === 1 && sent[0] === expected && sent[0].length + 1 <= 32,
      'type 8 TXTO keeps source/report counters and fits 32 bytes including NUL');
    check(warning.txTimeoutNeedReport === 0, 'successful diagnostic TX clears pending marker');
    vm.runInContext('reportPendingMessages()', warningContext);
    check(sent.length === 1 && elapsed === 1000, 'repeated dispatcher never duplicates warning');
    warning.txTimeoutNeedReport = 1; failWarning = true;
    assert.throws(() => vm.runInContext('reportPendingMessages()', warningContext), /warning TX reset/); cases++;
    check(warning.txTimeoutNeedReport === 1, 'failed TX does not clear diagnostic before reset');
  }
  {
    const dispatch = body(main, 'OnMaster');
    check((dispatch.match(/openRxWindow5000\(\);\s*(?:\/\*[\s\S]*?\*\/\s*)?reportPendingMessages\(\);/g) || []).length === 2,
      'time and GPS reports dispatch warnings only after the RX window returns');
    check(!body(main, 'loraTx').includes('sendTxTimeoutWarning(') &&
      !body(main, 'openRxWindow5000').includes('sendTxTimeoutWarning('),
      'no warning TX interrupts the receive window');
  }
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
for(const mode of ['normal','spurious','frozen','jump','cold-boot','fault-boot']) {
  let rtc=0,rawFeeds=0,sleeps=0,spur=0,awakeTick=5,windowFeeds=0;
  const enabled=new Set([8,39]);
  const state={rtcSeen:0,rtcValue:0,rtcTick:0,phaseTick:0,phaseLimit:0,
    feedTick:0,currentStage:0,BKP:{},IWDG:{},STAGE_STOP:10,STAGE_MAIN:4,
    RESET_RTC:2,HAL_GetTick:()=>awakeTick,Safety_HadFault:()=>mode==='cold-boot'?0:1,
    firstWindowDone:0,wdg_feed:()=>{windowFeeds++;},
    HAL_Delay:ms=>{const before=Math.floor(awakeTick/1000);awakeTick+=ms;rtc+=Math.floor(awakeTick/1000)-before;},
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
  const translated=js(stop).replace('let firstWindowDone = 0;','').replace('MODIFY_REG(PWR.CR, PWR_CR_PDDS | PWR_CR_LPDS, PWR_CR_LPDS | PWR_CR_CWUF)',
    'PWR.CR = (PWR.CR & ~(PWR_CR_PDDS | PWR_CR_LPDS)) | PWR_CR_LPDS | PWR_CR_CWUF');
  vm.runInContext('function enterStopUntil(wakeTs){'+translated+'}',stopctx);
  if(mode==='frozen'){
    assert.throws(()=>vm.runInContext('enterStopUntil(9)',stopctx),/IWDG expiry/);cases++;
    check(rawFeeds===1,'frozen RTC/spurious wakes cannot keep feeding');
  } else if(mode==='jump'){
    assert.throws(()=>vm.runInContext('enterStopUntil(9)',stopctx),/reset:2/);cases++;
    check(rawFeeds===1,'RTC overrun resets before post-STOP feed');
  } else {
    const target=mode==='cold-boot'?19:9;
    vm.runInContext('enterStopUntil('+target+')',stopctx);
    check(rtc===target && rawFeeds===6,'three successful RTC segments feed at boundaries');
    check(enabled.has(8)&&enabled.has(39)&&state.SysTick.CTRL===3,'restore IRQs and tick after STOP');
    if(mode==='spurious')check(sleeps===6,'spurious wakes add no feeds');
    if(mode==='cold-boot'){
      check(awakeTick===10005 && windowFeeds===100,'cold boot preserves full 10-second flashing window and feeds');
      vm.runInContext('enterStopUntil(28)',stopctx);
      check(awakeTick===10005 && windowFeeds===100 && rtc===28,'later sleep never repeats flashing delay');
    }
    if(mode==='fault-boot')check(awakeTick===5 && windowFeeds===0,'fault recovery does not repeatedly delay 10 seconds');
  }
}
check(stop.includes('NVIC_GetEnableIRQ(EXTI2_IRQn)') && stop.includes('NVIC_GetEnableIRQ(USART3_IRQn)'), 'save both IRQ enables');
check(stop.includes('if (savedRadioIrq) HAL_NVIC_EnableIRQ(EXTI2_IRQn)') && stop.includes('if (savedGpsIrq) HAL_NVIC_EnableIRQ(USART3_IRQn)'), 'restore both IRQ enables');
check(stop.indexOf('if ((int32_t)(after - target) >= 0) break;') < stop.indexOf('Safety_AfterStop();'), 'RTC deadline precedes STOP feed');
check(!stop.includes('loraInitFirstTime()'), 'segment wake does not reinitialize radio');
check(read('MDK-ARM/startup_stm32f103xb.s').includes('DCD     RTCAlarm_IRQHandler') && read('Core/Src/stm32f1xx_it.c').includes('void RTCAlarm_IRQHandler(void)'), 'RTC vector resolves');
check(!body(main,'checkVersionReportFlag').includes('Safety_MarkLinkHealthy'), 'version changes cannot erase a failure streak');
// Battery now gates GPS only; LoRa has no boot-count or low-battery skip.
const minimum=Number(main.match(/#define GPS_MIN_BATTERY_PCT (\d+)u/)[1]);
check(minimum===30,'GPS threshold remains 30 percent');
check(!main.includes('BATTERY_SKIP_') && !main.includes('skip LoRa, next cycle'),'old LoRa battery gate removed');
for(const count of [0,100,101,10000])for(const battery of [0,29,30,99]){
  const gpsctx=vm.createContext({GPS_MIN_BATTERY_PCT:minimum,cachedBatteryPercent:battery,bootTxRounds:count,
    gpsWaitReport:0,isInGpsWorkWindowAt:()=>1});
  install(gpsctx,main,[['gpsBatteryOk',''],['gpsNeededTimelyAt','ts']]);
  check(vm.runInContext('gpsBatteryOk()',gpsctx)===(battery>=30),'GPS battery boundary '+count+'/'+battery);
  check(Boolean(vm.runInContext('gpsNeededTimelyAt(100)',gpsctx))===(battery>=30),'no early GPS wake below threshold');
}
// A low-battery report sends time/battery and opens RX even inside GPS hours.
function reportSource(){
  return js(body(main,'OnMaster'))
    .replace(/#if TX_COUNT_ENABLE\s*([\s\S]*?)#else[\s\S]*?#endif/g,'$1')
    .replace(/\bchar\s+(\w+)\[[^\]]+\]/g,'let $1=[]');
}
for(const battery of [0,29,30]){
  let tx=0,rx=0,gps=0,calc=0,marks=0;
  const app={cachedBatteryPercent:99,GPS_MIN_BATTERY_PCT:minimum,
    getTodaySecond:()=>100,GetBatteryPercent:()=>battery,isInGpsWorkWindow:()=>1,
    gpsWaitReport:battery<30?1:0,gpsContTotalMin:battery<30?60:0,gpsPreSearched:1,gpsPreFixOk:1,gpsPreSec:10,gpsPreBytes:512,
    TX_LEN:64,MSG_TYPE_TIME:2,deviceName:'v3-5',tx_count:0,
    txCountNext:()=>{app.tx_count++;},markPeriodicReport:()=>{marks++;},sizeof:()=>64,snprintf:()=>{},loraTx:()=>{tx++;},
    openRxWindow5000:()=>{rx++;app.gpsWaitReport=1;app.gpsContTotalMin=60;},reportPendingMessages:()=>{},
    gpsPowerOn:()=>{gps++;},gpsPowerOff:()=>{},gpsRunContinuous:()=>{gps++;return 1;},
    sendGpsPositionMessage:()=>{gps++;},rtc_read_counter:()=>100,printf:()=>{},
    fastStartRounds:3,calcNextTxTime:()=>{calc++;},getCurrentTimestamp:()=>100,
    nextTxTimestamp:120,formatTimestamp:()=>{},loraSleep:()=>{},loraInitialized:1};
  const appctx=vm.createContext(app);install(appctx,main,[['gpsBatteryOk','']]);
  vm.runInContext('function OnMaster(){'+reportSource()+'}',appctx);
  vm.runInContext('OnMaster()',appctx);
  if(battery<30){
    check(tx===1 && rx===1 && gps===0 && calc===1 && marks===1,'low battery preserves LoRa TX/RX and actual-report anchor');
    check(app.gpsWaitReport===0&&app.gpsContTotalMin===0&&app.gpsPreSearched===0,'discard low-battery GPS requests/cache');
  }else check(gps===1&&rx===1,'30 percent still permits cached GPS reporting');
}
check(body(main,'gpsPowerOn').includes('if (!gpsBatteryOk()) return;'),'direct power-on has GPS battery gate');
check(body(main,'gpsPreSearch').includes('if (!gpsBatteryOk()) return;'),'presearch has GPS battery gate');
check(!main.includes('gpsCheckBattery') && !main.includes('batteryTick'),'periodic GPS battery polling removed');
for(const name of ['gpsRunContinuous','gpsWaitForFix'])
  check(body(main,name).includes('if (!gpsBatteryOk()) return 0u;') && !body(main,name).includes('GetBatteryPercent'),'GPS tasks check cached battery only at entry');
check(!body(main,'linkSilenceCheck').includes('cachedBatteryPercent'),'low battery does not disable link supervision');
// Actual upgps command branch rejects both single and continuous requests low.
const parser=body(main,'parse_rx_message');
const commandStart=parser.indexOf('else if (strcmp(fields[2], "upgps")');
const commandEnd=parser.indexOf('        /* sendmode:',commandStart);
assert(commandStart>=0 && commandEnd>commandStart);
const command=js(parser.slice(commandStart,commandEnd).replace(/^else if/,'if'));
for(const battery of [29,30])for(const minutes of [0,1,60]){
  const cmdctx=vm.createContext({GPS_MIN_BATTERY_PCT:minimum,cachedBatteryPercent:battery,
    fields:['9','v3-5','upgps',String(minutes)],field_count:4,strcmp:(a,b)=>a===b?0:1,
    cmdIsForMe:()=>1,atoi:s=>parseInt(s,10),GPS_CONT_MAX_MIN:60,GPS_CONT_INTERVAL_SEC:180,
    gpsSearchMaxMs:240000,gpsContTotalMin:0,gpsWaitReport:0,printf:()=>{}});
  install(cmdctx,main,[['gpsBatteryOk','']]);
  vm.runInContext('do {'+command+'} while(0);',cmdctx);
  check(cmdctx.gpsWaitReport===(battery>=30?1:0) && cmdctx.gpsContTotalMin===(battery>=30?minutes:0),'upgps battery policy '+battery+'/'+minutes);
}
for(const battery of [29,30]){
  const writes=[];
  const powerctx=vm.createContext({GPS_MIN_BATTERY_PCT:minimum,cachedBatteryPercent:battery,
    gpsPowerState:0,gpsRd:0,gpsWr:1,gpsLineLen:10,gpsRxCount:100,
    GNSS_WAKE_GPIO_Port:1,GNSS_WAKE_Pin:1,GPS_PWR_GPIO_Port:1,GPS_PWR_Pin:15,
    GPIO_PIN_SET:1,GPIO_PIN_RESET:0,GPS_PWR_ON_DELAY_MS:1000,
    HAL_GPIO_WritePin:(port,pin,value)=>writes.push([pin,value]),HAL_Delay:()=>{},printf:()=>{}});
  install(powerctx,main,[['gpsBatteryOk',''],['gpsPowerOn','']]);
  vm.runInContext('gpsPowerOn()',powerctx);
  check(JSON.stringify(writes)===JSON.stringify(battery>=30?[[1,1],[15,0]]:[]),'low battery never asserts GPS power');
  check(powerctx.gpsPowerState===(battery>=30?1:0),'GPS power state respects threshold');
}
// Ordinary battery sampling still uses the bounded ADC phase.
{
  const stages=[];
  const adcctx=vm.createContext({ADC1:{CR2:0},ADC_CR2_ADON:1,HAL_Delay:()=>{},ADC1_Read:()=>2400,
    STAGE_ADC:12,STAGE_MAIN:4,Safety_Begin:(stage,limit)=>stages.push([stage,limit]),printf:()=>{}});
  install(adcctx,main,[['GetBatteryPercent','']]);
  vm.runInContext('GetBatteryPercent()',adcctx);
  check(JSON.stringify(stages)===JSON.stringify([[12,2000],[4,15000]]),'ordinary ADC phase retained');
}
// Started searches finish by fix/timeout without any in-task battery sampling.
for(const mode of ['initial-low','timeout','fix']){
  let now=0,feeds=0;
  const searchctx=vm.createContext({cachedBatteryPercent:mode==='initial-low'?29:99,GPS_MIN_BATTERY_PCT:minimum,
    HAL_GetTick:()=>now,HAL_Delay:ms=>{now+=ms;},gpsTask:()=>{},gpsFixFresh:()=>mode==='fix',
    gpsPrintEn:0,STAGE_GPS:8,Safety_Begin:()=>{},printf:()=>{},wdg_feed:()=>{feeds++;},
    GetBatteryPercent:()=>{throw Error('Unexpected battery sample during search');}});
  install(searchctx,main,[['gpsBatteryOk',''],['gpsWaitForFix','minMs,timeoutMs']]);
  const result=vm.runInContext('gpsWaitForFix(10000,30000)',searchctx,{timeout:1000});
  check(result===(mode==='fix'?1:0),'search result '+mode);
  check(now===(mode==='initial-low'?0:mode==='fix'?10000:30000),'entry gate or original search deadline '+mode);
}
// Started tracking retains its original interval and no-fix extension rules.
for(const mode of ['initial-low','normal','multiple','no-fix']){
  let now=0,searches=0;
  const track={cachedBatteryPercent:mode==='initial-low'?29:99,GPS_MIN_BATTERY_PCT:minimum,
    HAL_GetTick:()=>now,GPS_CONT_MAX_SEC:3600,GPS_CONT_INTERVAL_SEC:180,
    STAGE_TRACK_WAIT:9,Safety_Begin:()=>{},printf:()=>{},gpsTask:()=>{},wdg_feed:()=>{},
    HAL_Delay:ms=>{now+=ms;},GetBatteryPercent:()=>{throw Error('Unexpected battery sample during tracking');},
    gpsSearchAndReport:()=>{searches++;now+=10000;return mode==='no-fix'?0:1;}};
  const trackctx=vm.createContext(track);install(trackctx,main,[['gpsBatteryOk','']]);
  const translated=js(body(main,'gpsRunContinuous'));
  vm.runInContext('function gpsRunContinuous(totalMin,useGpsType){'+translated+'}',trackctx);
  const result=vm.runInContext('gpsRunContinuous('+(mode==='multiple'?10:1)+',0)',trackctx,{timeout:1000});
  check(searches===(mode==='initial-low'?0:mode==='multiple'?4:mode==='no-fix'?20:1),'tracking search count '+mode);
  check(now===(mode==='initial-low'?0:mode==='multiple'?550000:mode==='no-fix'?3430000:10000) && result===(mode==='normal'||mode==='multiple'?1:0),'original tracking duration '+mode);
}
const countctx=vm.createContext({tx_count:999,TX_COUNT_MAX:999,bootTxRounds:100,WINDOW_PRINT_MAX_ROUNDS:10,windowPrintOff:0});
install(countctx,main,[['txCountNext','']]);
vm.runInContext('txCountNext()',countctx);
check(countctx.bootTxRounds===101 && countctx.tx_count===1,'boot count survives wire counter wrap');
// Execute current slot calculation with real C integer-division semantics.
function installSchedule(context){
  const translated=js(body(main,'calcNextTxTime'))
    .replace('rtc_read_counter() - periodicLastRtc','((rtc_read_counter() - periodicLastRtc) >>> 0)')
    .replace(/intervalSec \/ 2\b/g,'Math.floor(intervalSec / 2)')
    .replace('earliest / 86400','Math.floor(earliest / 86400)')
    .replace('tod / intervalSec','Math.floor(tod / intervalSec)');
  vm.runInContext('function calcNextTxTime(){'+translated+'}',context);
  install(context,main,[['markPeriodicReport',''],['scheduleAfterSync','']]);
}
const scheduleEnv={timeSynced:1,fastStartRounds:0,period:600,wall:0,index:5,count:30,rtc:0,
  getConfigIntervalSec:()=>scheduleEnv.period,getCurrentTimestamp:()=>scheduleEnv.wall,
  getSlotOffsetSec:p=>Math.floor((scheduleEnv.index%scheduleEnv.count)*p/scheduleEnv.count),
  rtc_read_counter:()=>scheduleEnv.rtc,periodicSeen:0,periodicLastRtc:0xFFFFFF00,nextTxTimestamp:0,printf:()=>{}};
const scheduleCtx=vm.createContext(scheduleEnv);installSchedule(scheduleCtx);
const scheduleScript=new vm.Script('calcNextTxTime()');
function scheduleCase(wall,period,index,count,elapsed=null,fast=0){
  Object.assign(scheduleEnv,{wall,period,index,count,fastStartRounds:fast,
    rtc:elapsed===null?10000:((0xFFFFFF00+elapsed)>>>0),periodicSeen:elapsed!==null?1:0});
  scheduleScript.runInContext(scheduleCtx);
  return scheduleEnv.nextTxTimestamp;
}
const h=(hours,minutes,seconds=0)=>hours*3600+minutes*60+seconds;
check(scheduleCase(h(19,0,5),600,5,30)===h(19,1,40),'current-period slot remains eligible');
check(scheduleCase(h(19,6,41),600,5,30)===h(19,11,40),'short remaining wait no longer skips a period');
check(scheduleCase(h(19,8),600,5,30,1000)===h(19,11,40),'old regular report permits nearest slot');
check(scheduleCase(h(19,8),600,5,30,0)===h(19,21,40),'recent regular report enforces spacing');
check(scheduleCase(h(19,8),600,5,30,80)===h(19,11,40),'exact half-period total spacing allowed');
check(scheduleCase(h(19,8),600,5,30,79)===h(19,21,40),'one second short still protected');
check(scheduleCase(h(19,1,40),600,5,30)===h(19,11,40),'exact current slot is not retransmitted');
// Reference chooses the first future eligible slot, enumerating each day.
function expectedSlot(wall,period,offset,elapsed){
  const gap=elapsed===null?1:Math.max(1,Math.floor(period/2)-elapsed);
  const midnight=wall-wall%86400;
  for(let day=0;day<2;day++)for(let tod=offset;tod<86400;tod+=period){
    const candidate=midnight+day*86400+tod;
    if(candidate>=wall+gap)return candidate;
  }
  throw Error('No reference slot');
}
for(let minutes=1;minutes<=60;minutes++)for(const count of [1,30,60])for(const index of [0,5,count-1])
  for(const tod of [0,5,59,12345,86300,86399])for(const elapsed of [null,0,29,300,4000]){
    const period=minutes*60,wall=20000*86400+tod,offset=Math.floor((index%count)*period/count);
    check(scheduleCase(wall,period,index,count,elapsed)===expectedSlot(wall,period,offset,elapsed),
      'nearest eligible daily slot '+[minutes,count,index,tod,elapsed]);
  }
check(scheduleCase(h(19,0,5),600,5,30,null,3)===h(19,0,10),'startup fast period uses its own offset');
{
  let rtc=100,wall=1000;
  const context=vm.createContext({timeSynced:0,fastStartRounds:0,tx_count:0,
    getConfigIntervalSec:()=>600,getCurrentTimestamp:()=>wall,getSlotOffsetSec:()=>100,
    rtc_read_counter:()=>rtc,periodicSeen:0,periodicLastRtc:0,nextTxTimestamp:0,printf:()=>{}});
  installSchedule(context);
  vm.runInContext('calcNextTxTime()',context);check(context.nextTxTimestamp===1600,'unsynced interval unchanged');
  vm.runInContext('scheduleAfterSync()',context);check(context.nextTxTimestamp===1000,'first boot immediate report unchanged');
  vm.runInContext('markPeriodicReport()',context);check(context.periodicSeen===1&&context.periodicLastRtc===100,'completed regular TX records RTC');
  context.timeSynced=1;context.tx_count=1;rtc=180;
  for(const corrected of [h(19,8),h(18,8),h(20,8)]){
    wall=corrected;vm.runInContext('scheduleAfterSync()',context);
    check(context.nextTxTimestamp===expectedSlot(wall,600,100,80),'forward/backward wall sync keeps RTC spacing');
    check(context.periodicLastRtc===100,'rescheduling cannot move actual-TX anchor');
  }
}
// The current firmware includes later GPS/log updates; preserve this task's
// pre-change bodies instead of comparing against the November 23 snapshot.
const beforeSchedule=read('backups/rx_fallback_20261127/Core/Src/main.c');
for(const name of ['gpsRunContinuous','gpsWaitForFix','gpsSearchAndReport'])
  check(body(main,name)===body(beforeSchedule,name),'UPGPS timing function unchanged '+name);
check(body(main,'sendGpsPositionMessage').includes('if (useGpsType) markPeriodicReport();'),'type 5 never updates ordinary TX anchor');
for(const useType1 of [0,1])for(const fail of [false,true]){
  const sender=vm.createContext({TX_LEN:64,MSG_TYPE_GPS:1,MSG_TYPE_UP_GPS:5,
    deviceName:'v3-5',tx_count:1,txCountNext:()=>{},fmtRxBytes:()=>{},sizeof:()=>64,snprintf:()=>{},
    loraTx:()=>{if(fail)throw Error('TX reset');},rtc_read_counter:()=>123,
    periodicSeen:0,periodicLastRtc:0});
  install(sender,main,[['markPeriodicReport','']]);
  const send=js(body(main,'sendGpsPositionMessage'))
    .replace(/#if TX_COUNT_ENABLE\s*([\s\S]*?)#else[\s\S]*?#endif/g,'$1')
    .replace(/char\s+latBuf\[16\], lngBuf\[16\];/,'let latBuf=[], lngBuf=[];')
    .replace(/\bchar\s+(\w+)\[[^\]]+\]/g,'let $1=[]');
  vm.runInContext('function sendGpsPositionMessage(hasFix,useGpsType,searchSec,rxBytes){'+send+'}',sender);
  if(fail){assert.throws(()=>vm.runInContext('sendGpsPositionMessage(0,'+useType1+',10,0)',sender),/TX reset/);cases++;}
  else vm.runInContext('sendGpsPositionMessage(0,'+useType1+',10,0)',sender);
  check(sender.periodicSeen===(!fail&&useType1?1:0),'only completed scheduled GPS anchors spacing');
}
for(const name of ['sendCenterMessage','sendConfigMessage','sendResetInfoMessage','sendReadInfoMessage'])
  check(!body(main,name).includes('markPeriodicReport'),'auxiliary frames cannot defer regular slots '+name);
// Inject actual scheduleAfterSync repeatedly inside unchanged tracking loops.
// Forward/backward wall-clock jumps and slot recalculation cannot alter traces.
function trackingTrace(jumps,noFix){
  let tick=0,wallBase=h(19,0),syncs=0;const trace=[];
  const context=vm.createContext({cachedBatteryPercent:99,GPS_MIN_BATTERY_PCT:30,
    timeSynced:1,fastStartRounds:0,tx_count:1,getConfigIntervalSec:()=>600,
    getCurrentTimestamp:()=>wallBase+Math.floor(tick/1000),getSlotOffsetSec:()=>100,
    rtc_read_counter:()=>100+Math.floor(tick/1000),periodicSeen:1,periodicLastRtc:90,
    HAL_GetTick:()=>tick,GPS_CONT_MAX_SEC:3600,GPS_CONT_INTERVAL_SEC:180,
    STAGE_TRACK_WAIT:9,Safety_Begin:()=>{},printf:()=>{},wdg_feed:()=>{},
    HAL_Delay:ms=>{tick+=ms;},GetBatteryPercent:()=>{throw Error('Unexpected in-task sample');},
    gpsTask:()=>{
      if(jumps&&Math.floor(tick/5000)>syncs){syncs=Math.floor(tick/5000);wallBase+=syncs%2?3600:-7200;
        vm.runInContext('scheduleAfterSync()',context);}
    },gpsSearchAndReport:()=>{trace.push(tick);tick+=10000;
      if(jumps){wallBase+=trace.length%2?86400:-172800;vm.runInContext('scheduleAfterSync()',context);}
      return noFix?0:1;}});
  installSchedule(context);install(context,main,[['gpsBatteryOk',''],['gpsRunContinuous','totalMin,useGpsType']]);
  const result=vm.runInContext('gpsRunContinuous(10,0)',context,{timeout:5000});
  check(context.periodicLastRtc===90,'UPGPS and sync never refresh ordinary TX anchor');
  return {trace,tick,result};
}
for(const noFix of [false,true])
  check(JSON.stringify(trackingTrace(false,noFix))===JSON.stringify(trackingTrace(true,noFix)),
    'UPGPS interval/deadline unchanged by repeated wall time corrections '+noFix);
// No wake countdown; the MCU stays asleep until one-second radio preparation.
const beforeWake=beforeSchedule;
check(stop===body(beforeWake,'enterStopUntil'),'original first-sleep flashing window unchanged byte-for-byte');
for(const name of ['gpsRunContinuous','gpsWaitForFix','gpsSearchAndReport'])
  check(body(main,name)===body(beforeWake,name),'wake optimization cannot alter UPGPS timing '+name);
const radioLead=Number(main.match(/#define RADIO_WAKE_LEAD_SEC (\d+)u/)[1]);
check(radioLead===1 && !main.includes('for (int i = 5; i > 0; i--)'),'one-second preparation without five-second countdown');
const wakeStart=main.indexOf('      uint8_t  timelyGps   = gpsNeededTimelyAt(nextTxTimestamp);');
const wakeEnd=main.indexOf('      /*',main.indexOf('        enterStopUntil(wakeTime);',wakeStart));
assert(wakeStart>=0&&wakeEnd>wakeStart);
const wakeCode=js(main.slice(wakeStart,wakeEnd));
for(const scenario of [
  {now:598,target:600,gps:false,interval:600,init:0,sleep:599},
  {now:599,target:600,gps:false,interval:600,init:1,sleep:null},
  {now:1,target:0,gps:false,interval:600,init:1,sleep:null},
  {now:358,target:600,gps:true,interval:600,init:0,sleep:359},
  {now:359,target:600,gps:true,interval:600,init:1,sleep:599},
  {now:40,target:100,gps:true,interval:60,init:0,sleep:41},
  {now:359,target:600,gps:true,interval:600,init:1,sleep:null,searchSeconds:240},
  {now:359,target:600,gps:true,interval:600,init:1,sleep:null,searchSeconds:245},
  {now:359,target:600,gps:true,interval:600,init:1,sleep:599,searchSeconds:0},
  {now:359,target:600,gps:true,interval:600,init:1,sleep:599,battery:29},
  {now:500,target:600,gps:true,interval:600,init:0,sleep:599,cached:1},
  {now:599,target:600,gps:true,interval:600,init:1,sleep:null,cached:1}
]){
  let initialized=0,slept=null,searches=0,gpsPowered=false,clockReady=false;
  const events=[];
  const wake={nowTs:scenario.now,nextTxTimestamp:scenario.target,gpsPreSearched:scenario.cached||0,gpsContTotalMin:0,
    RADIO_WAKE_LEAD_SEC:radioLead,GPS_PRESEARCH_LEAD_SEC:241,
    gpsNeededTimelyAt:()=>scenario.gps&&wake.cachedBatteryPercent>=30?1:0,getConfigIntervalSec:()=>scenario.interval,
    SystemClock_Config:()=>{clockReady=true;events.push('clock');},
    GetBatteryPercent:()=>scenario.battery??99,cachedBatteryPercent:99,
    loraInitFirstTime:()=>{
      check(clockReady && !gpsPowered,'restore clocks and power off GPS before radio init');
      initialized++;wake.loraInitialized=1;events.push('radio');
    },printf:()=>{},
    HAL_Delay:()=>{throw Error('Unexpected fixed wake delay');},
    gpsPreSearch:()=>{
      check(initialized===0 && wake.loraInitialized===0,'radio stays asleep throughout automatic search');
      searches++;gpsPowered=true;events.push('gps');
      wake.nowTs+=scenario.searchSeconds??10;wake.gpsPreSearched=1;gpsPowered=false;
    },
    getCurrentTimestamp:()=>wake.nowTs,loraSleep:()=>{},loraInitialized:0,
    enterStopUntil:time=>{
      check(initialized===0 && !gpsPowered,'both modules remain inactive while awaiting TX');
      slept=time;wake.nowTs=time;clockReady=false;events.push('stop');
    }};
  vm.runInContext(wakeCode,vm.createContext(wake));
  check(initialized===scenario.init && slept===scenario.sleep,'wake only at actual preparation deadline '+scenario.now+'/'+scenario.target);
  check(searches===(scenario.gps&&scenario.init&&!scenario.cached&&scenario.battery!==29?1:0),'automatic GPS preparation still scheduled');
  if(searches)check(events.indexOf('gps')<events.indexOf('radio'),'radio initialization follows GPS completion');
  if(initialized&&slept!==null)check(events.slice(-2).join(',')==='clock,radio','STOP restores clocks immediately before radio init');
}
const beforeDeferredRadio=beforeSchedule;
for(const name of ['gpsRunContinuous','gpsWaitForFix','gpsSearchAndReport','enterStopUntil'])
  check(body(main,name)===body(beforeDeferredRadio,name),'deferred radio preserves UPGPS and first flash window '+name);
check(!body(main,'gpsPreSearch').includes('loraInitFirstTime'),'automatic search never initializes radio internally');
const summary={passed:cases,scope:'source-derived RTC/IRQ/sleep decision regressions; no board execution'};
fs.writeFileSync(path.join(root,'MDK-ARM/watchdog_build/test_low_power_results.json'),JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary,null,2));
