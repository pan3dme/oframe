const fs=require('fs');
let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
const exact=['[BOOT] step: rtc','[BOOT] step: flash load','[BOOT] step: cfg ok','TX mode: tx-only','[SCHED] ready, first tx now','[WAKE] 5s countdown','[GPS] skip presearch, live search','[GPS] upgps first: type5 no window','[GPS] type1 tx, then window','[SCHED] retry next round (skip time frame)','[SCHED] resume normal time frame','[SCHED] GPS window, GPS frame'];
// Replace diagnostic-only messages with empty statements, retaining if/else syntax.
for(const text of exact) {
  const a='printf("'+text+'\\r\\n");';
  if(!s.includes(a))throw Error('Missing log '+text);
  s=s.replace(a,'; /* redundant serial status omitted */');
}
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
