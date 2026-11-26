/* Read-only firmware audit. Executes selected C function bodies after mechanical
 * syntax conversion, with mocked peripherals. NOT MCU execution or RF testing.
 * Writes only evidence.json beside this script; never changes build artifacts. */
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm');
const assert = require('assert'), crypto = require('crypto');
const root = path.resolve(__dirname, '../..');
const read = p => fs.readFileSync(path.join(root, p), 'latin1');
const main = read('Core/Src/main.c'), radio = read('Radio/src/pan3029_rf.c');
const safety = read('Core/Src/reliability.c');
function body(src, name) {
  const m = src.match(new RegExp('^(?:static )?[\\w* ]+\\b' + name + '\\([^)]*\\)\\s*\\r?\\n\\{([\\s\\S]*?)^\\}', 'm'));
  assert(m, 'Missing function: ' + name);
  return m[1];
}
function js(s) {
  return s.replace(/#if SPI_WRITE_CHECK[\s\S]*?#endif/g, '')
    .replace(/\(uint32_t\)\((now - phaseTick|HAL_GetTick\(\) - start)\)/g, '(($1) >>> 0)')
    .replace(/\((?:uint8_t|uint16_t|uint32_t|int32_t|unsigned long|uint8_t\s*\*)\)/g, '')
    .replace(/\b(?:uint8_t|uint16_t|uint32_t|int32_t)\b/g, 'let')
    .replace(/\b(0x[\da-f]+|\d+)[uU]\b/gi, '$1').replace(/->/g, '.')
    .replace(/&([se])\b/g, '$1');
}
function install(ctx, src, name, args) {
  vm.runInContext('function ' + name + '(' + args + '){' + js(body(src, name)) + '}', ctx);
}
const probes = [];
// The application can continue feeding while a silent GPS consumes its entire
// configured search budget; the function contains no module recovery action.
for (const timeout of [60000, 240000, 600000]) {
  let tick = 1, feeds = 0, tasks = 0;
  const env = { feedTick: 0, phaseTick: 0, phaseLimit: 0, currentStage: 0,
    STAGE_GPS: 8, RESET_DEADLINE: 16, BKP: {}, IWDG: {},
    gpsBatteryOk: () => true, HAL_GetTick: () => tick,
    HAL_Delay: ms => { tick += ms; }, gpsTask: () => { tasks++; },
    gpsFixFresh: () => false, gpsPrintEn: false, printf: () => {},
    Safety_Reset: reason => { throw Error('reset:' + reason); } };
  Object.defineProperty(env.IWDG, 'KR', { set: () => feeds++ });
  const ctx = vm.createContext(env);
  install(ctx, safety, 'Safety_Begin', 'stage,maxMs');
  install(ctx, safety, 'Safety_Feed', '');
  env.wdg_feed = () => vm.runInContext('Safety_Feed()', ctx);
  install(ctx, main, 'gpsWaitForFix', 'minMs,timeoutMs');
  const result = vm.runInContext('gpsWaitForFix(10000,' + timeout + ')', ctx);
  assert.strictEqual(result, 0); assert.strictEqual(tick - 1, timeout);
  assert(feeds > 0); assert(!/gpsPowerOn|GPS_RST|HAL_UART_Abort/.test(body(main, 'gpsWaitForFix')));
  probes.push({ name: 'silent GPS waits and feeds', timeoutMs: timeout,
    observedElapsedMs: tick - 1, feeds, taskCalls: tasks, result });
}
// Run actual loraTx with mocked successful TX to capture the exact supplied
// byte count, including its treatment of the NUL terminator on long messages.
for (const msg of ['2|v3-23|43200|99|999',
  '8|v3-23|INFO|60T|433HZ|20dbm|V2.8|999|',
  '8|v3-23|COORD|-90.00000,-180.00000|999|']) {
  let sent;
  const env = { LORA_TX_MAX: 36, STAGE_TX: 6, STAGE_MAIN: 4,
    RADIO_FLAG_IDLE: 0, RF_OK: 0, RESET_TX: 3, HAL_GetTick: () => 1,
    strlen: s => Buffer.byteLength(s, 'ascii'), Safety_Begin: () => {},
    rf_set_transmit_flag: () => {}, rf_get_transmit_flag: () => 1,
    rf_single_tx_data: (s, n) => { sent = Buffer.from(s + '\0', 'ascii').subarray(0, n); return 0; },
    Safety_Reset: r => { throw Error('reset:' + r); }, printf: () => {}, wdg_feed: () => {} };
  const ctx = vm.createContext(env);
  const code = js(body(main, 'loraTx')).replace('&tx_time', 'null');
  vm.runInContext('function loraTx(msg){' + code + '}', ctx);
  env.msg = msg; vm.runInContext('loraTx(msg)', ctx);
  const truncated = msg.length + 1 > 36;
  assert.strictEqual(sent.includes(0), !truncated);
  probes.push({ name: 'TX frame boundary', sourceText: msg,
    desiredBytes: msg.length + 1, actualBytes: sent.length,
    containsNul: sent.includes(0), actualAscii: sent.toString('ascii') });
}
// A completed MCU SPI transfer does not prove the external radio accepted it.
{
  let reads = 0, writes = 0;
  const ctx = vm.createContext({ OK: 0, FAIL: 1,
    rf_port: { spi_cs_low: () => {}, spi_cs_high: () => {},
      spi_readwrite: () => { writes++; return 0; } },
    rf_read_reg: () => { reads++; return 0; } });
  install(ctx, radio, 'rf_write_reg', 'addr,value');
  const result = vm.runInContext('rf_write_reg(4,54)', ctx);
  assert.strictEqual(result, 0); assert.strictEqual(reads, 0);
  probes.push({ name: 'blind radio write', result, readbackCalls: reads, transferredBytes: writes });
}
// Verify the boot dependency: without a known time, automatic GPS windows
// cannot start the GPS that would otherwise supply fallback time.
{
  const ctx = vm.createContext({ timeSynced: 0, gpsWaitReport: 0,
    gpsBatteryOk: () => true });
  install(ctx, main, 'isInGpsWorkWindowAt', 'ts');
  install(ctx, main, 'gpsNeededTimelyAt', 'ts');
  const needed = vm.runInContext('gpsNeededTimelyAt(0)', ctx);
  assert.strictEqual(needed, 0);
  probes.push({ name: 'unsynchronized automatic GPS gate', automaticGpsNeeded: needed });
}
// A low-level SPI reset aborts the advertised three-attempt radio retry loop.
{
  let resetPinWrites = 0, rfCalls = 0;
  const ctx = vm.createContext({ EXTI2_IRQn: 8, STAGE_RADIO: 5, STAGE_MAIN: 4,
    LORA_CSN_GPIO_Port: 2, LORA_CSN_Pin: 0, LORA_RST_GPIO_Port: 1, LORA_RST_Pin: 1,
    GPIO_PIN_SET: 1, GPIO_PIN_RESET: 0, GPIO_PIN_2: 2, currentTxPower: 16,
    HAL_NVIC_DisableIRQ: () => {}, HAL_NVIC_EnableIRQ: () => {},
    HAL_NVIC_ClearPendingIRQ: () => {}, __HAL_GPIO_EXTI_CLEAR_IT: () => {},
    Safety_Begin: () => {}, wdg_feed: () => {}, HAL_Delay: () => {},
    HAL_GPIO_WritePin: () => { resetPinWrites++; },
    rf_init: () => { rfCalls++; throw Error('RESET_SPI'); }, RF_OK: 0 });
  install(ctx, main, 'loraTryInit', '');
  assert.throws(() => vm.runInContext('loraTryInit()', ctx), /RESET_SPI/);
  assert.strictEqual(rfCalls, 1);
  probes.push({ name: 'SPI fault bypasses radio retries', rfCalls, gpioWritesBeforeReset: resetPinWrites });
}
const inputs = ['Core/Src/main.c', 'Core/Src/reliability.c', 'Core/Src/nvstore.c',
  'Core/Src/stm32f1xx_it.c', 'Core/Inc/main.h', 'Radio/src/pan3029_rf.c',
  'Radio/src/pan3029_port.c', 'Radio/inc/pan3029_port.h',
  'MDK-ARM/STM32_ONLY_SEND.uvprojx', 'tools/build_watchdog.cjs',
  'tools/test_watchdog.cjs', 'tools/test_low_power.cjs'];
const hashes = {};
for (const f of inputs) hashes[f] = crypto.createHash('sha256').update(fs.readFileSync(path.join(root, f))).digest('hex');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'MDK-ARM/watchdog_build/build_manifest.json'), 'utf8'));
const manifestMismatches = Object.entries(manifest.sources).filter(([f, h]) =>
  crypto.createHash('sha256').update(fs.readFileSync(path.join(root, f))).digest('hex') !== h).map(([f]) => f);
const evidence = { auditDate: '2026-11-26', version: 'V2.8 / APP_VERSION 19',
  scope: 'C-body-derived JavaScript probes with peripheral mocks; no board execution; no firmware build or flash',
  probes, hashes, legacyManifestCompiler: manifest.compiler, manifestMismatches,
  existingRegressionRuns: {
    watchdog: 'FAIL at source manifest Core\\Src\\main.c (test_watchdog.cjs:141)',
    lowPower: 'ERROR: openRxWindow4000 is not defined (test_low_power.cjs:252; mock still named openRxWindow3000)',
    note: 'Existing result JSON files were preserved. Neither regression completed against the current sources.'
  } };
fs.writeFileSync(path.join(__dirname, 'evidence.json'), JSON.stringify(evidence, null, 2) + '\n');
console.log(JSON.stringify({ probes: probes.length, assertions: 'all passed; findings reproduced',
  manifestMismatches, scope: evidence.scope }, null, 2));
