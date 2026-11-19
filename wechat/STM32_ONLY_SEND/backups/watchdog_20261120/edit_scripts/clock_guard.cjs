const fs=require('fs');let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
const anchor='  Safety_Begin(STAGE_CLOCK, 3000u);';
if(!s.includes(anchor))throw Error('clock anchor');
s=s.replace(anchor,'  /* Already running (e.g. first TX after boot): do not disable its PLL. */\r\n  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) return;\r\n'+anchor);
s=s.replace('  printf("\\r\\n===== LoRa TX-only boot =====\\r\\n");','  printf("\\r\\n[BOOT] LoRa\\r\\n");');
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
