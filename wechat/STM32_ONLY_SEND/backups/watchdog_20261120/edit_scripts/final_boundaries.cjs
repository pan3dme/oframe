const fs=require('fs');let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
function edit(a,b){if(!s.includes(a))throw Error(a.slice(0,80));s=s.replace(a,b);}
edit('realDelta < 1000000u','realDelta < 0x80000000u');
edit('rtcDriftPerHour && localDelta < 1000000u','rtcDriftPerHour');
const block=/  \{\r?\n    uint32_t backoff = Safety_BackoffSeconds\(\);[\s\S]*?^  \}\r?\n/m;
const found=s.match(block);if(!found)throw Error('backoff');
s=s.replace(block,'');
const earlier=found[0].replace('      enterStopUntil(getCurrentTimestamp() + backoff);','      enterStopUntil(getCurrentTimestamp() + backoff);\r\n      SystemClock_Config();');
edit('  rtc_init();','  rtc_init();\r\n'+earlier);
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
