const fs=require('fs');
let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
function edit(a,b){if(!s.includes(a))throw Error(a.slice(0,80));s=s.replace(a,b);}
edit('  printf("[FLASH] committed\\r\\n");','');
edit('  printf("[ADC] cal done CR2=0x%08lX\\r\\n", (unsigned long)ADC1->CR2);','');
edit('  GPIO_InitStruct.Pin   = GPS_PWR_Pin;', '  HAL_GPIO_WritePin(GPS_PWR_GPIO_Port, GPS_PWR_Pin, GPIO_PIN_SET);\r\n  GPIO_InitStruct.Pin   = GPS_PWR_Pin;');
edit('  RCC->APB2ENR |= (1u << 9);', '  GPIOA->CRL &= ~0xFu; /* PA0 analog input: avoid digital input leakage. */\r\n  RCC->APB2ENR |= (1u << 9);');
// A successful radio reset at the ordinary wake is already part of the protocol.
// Diagnostics remain compact so the Keil Lite build stays within 32 KiB.
edit('printf("[SCHED] fast boot %u rounds\\r\\n", (unsigned)fastStartRounds);', 'printf("[FAST] %u\\r\\n", (unsigned)fastStartRounds);');
// Remove obsolete comments that claimed software reset was a power cycle.
s=s.replace(/\/\* =====================================================================\r?\n \* HW watchdog[\s\S]*?\*\//,'/* IWDG: ~10s nominal, >=6.66s at 60kHz LSI. Three-second RTC\r\n * segments leave setup/wake margin; CPU reset does not cut module power. */');
s=s.replace(/\/\* [^\r\n]*32 [^\r\n]*\r?\n \* @note[\s\S]*?\*\/(?=\r?\nstatic uint32_t rtc_read_counter)/,'/* Re-sync APB shadow after STOP; high/low/high handles the carry race. */');
// Clear stale legacy descriptions immediately before the watchdog definition.
const p=s.indexOf('/**',s.indexOf('static uint8_t gpsNeededTimelyAt(uint32_t ts)\r\n{'));
const end=s.indexOf('#define WDG_SEG_SEC',p);
if(p>=0&&end>p)s=s.slice(0,p)+'/* Link health: valid protocol traffic, monotonic work-time accumulation. */\r\n'+s.slice(end);
// Replace comments above schedule reset/receive bookkeeping with current semantics.
const sync=s.indexOf('static void scheduleAfterSync(void)\r\n{');
const ls=s.indexOf('  linkLastRtc = rtc_read_counter();',sync);
if(ls<0)throw Error('sync bookkeeping');
s=s.slice(0,sync)+s.slice(sync,ls).replace(/  \/\*[\s\S]*?\*\/\r?\n/,'  /* Do not charge a wall-clock jump as elapsed link time. */\r\n')+s.slice(ls);
// Remove standalone empty log markers. If/else branches need explicit braces.
s=s.replace(/      if \(firstCmd\)\r?\n        ; \/\* redundant serial status omitted \*\/\r?\n      else\r?\n        ; \/\* redundant serial status omitted \*\//,'');
s=s.replace(/      if \(gpsNeedRetry\)\r?\n        ; \/\* redundant serial status omitted \*\/\r?\n      else\r?\n        ; \/\* redundant serial status omitted \*\//,'');
s=s.replace(/^ *; \/\* redundant serial status omitted \*\/\r?\n/gm,'');
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
