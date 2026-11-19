const fs=require('fs');
let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
const fix=(a,b)=>{if(!s.includes(a))throw Error(a.slice(0,100));s=s.replace(a,b);};
fix('    uint32_t nowTs;\r\n    Safety_Begin(STAGE_MAIN, 15000u);\r\n    nowTs = getCurrentTimestamp();\r\n    linkSilenceCheck();', '    uint32_t nowTs = getCurrentTimestamp();');
const main=s.indexOf('int main(void)');
const anchor='    uint32_t nowTs = getCurrentTimestamp();';
const at=s.indexOf(anchor,main);if(at<0)throw Error('main loop');
s=s.slice(0,at)+`    uint32_t nowTs;
    Safety_Begin(STAGE_MAIN, 15000u);
    nowTs = getCurrentTimestamp();
    linkSilenceCheck();`+s.slice(at+anchor.length);
// Count the long blocking tracking interval as a paused link-health interval.
fix('          gpsContTotalMin = 0;', '          linkLastRtc = rtc_read_counter();\r\n          gpsContTotalMin = 0;');
// Validate the base62 index, not only its character spelling.
fix('  return 1;                        /*', '  return twoCharToIndex(f) >= 0 && twoCharToIndex(f) <= 275; /*');
// Obsolete window helper and old watchdog comments no longer describe policy.
s=s.replace(/^static uint8_t isInWorkWindow\(void\);[^\r\n]*\r?\n/m,'');
s=s.replace(/^static uint8_t isInWorkWindow\(void\)\r?\n\{[\s\S]*?^\}\r?\n/m,'');
// The compiler could otherwise observe a 32-bit drift difference overflow.
fix('int32_t  driftSec = (int32_t)localElapsedSec - (int32_t)remoteElapsedSec;', 'int64_t  driftSec = (int64_t)localElapsedSec - (int64_t)remoteElapsedSec;');
// Restart HSE after a fallback even if a previous HSI PLL was still enabled.
fix('    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;', '    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;\r\n    osc.PLL.PLLState = RCC_PLL_OFF;\r\n    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Safety_Reset(RESET_INIT);\r\n    osc.PLL.PLLState = RCC_PLL_ON;');
fs.writeFileSync('Core/Src/main.c',Buffer.from(s.replace(/(?<!\r)\n/g,'\r\n'),'latin1'));
