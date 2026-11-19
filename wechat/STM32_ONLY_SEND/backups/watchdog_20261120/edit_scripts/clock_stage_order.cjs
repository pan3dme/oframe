const fs=require('fs');let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
const a='  Safety_Begin(STAGE_CLOCK, 3000u);\r\n  /* Already running (e.g. first TX after boot): do not disable its PLL. */\r\n  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) return;';
if(!s.includes(a))throw Error('clock stage order');
s=s.replace(a,'  /* Already running (e.g. first TX after boot): do not disable its PLL. */\r\n  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) return;\r\n  Safety_Begin(STAGE_CLOCK, 3000u);');
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
