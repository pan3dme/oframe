/* Boundary tests + a power-loss model. This is NOT a board/CPU emulator. */
const fs=require('fs'),assert=require('assert'),vm=require('vm'),path=require('path');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'latin1');
const main=read('Core/Src/main.c'),safety=read('Core/Src/reliability.c');
const nv=read('Core/Src/nvstore.c');
let cases=0;
function check(v,message){assert(v,message);cases++;}
function body(src,name){const r=new RegExp('(?:static )?(?:void|uint32_t) '+name+'\\([^)]*\\)\\r?\\n\\{([\\s\\S]*?)^\\}','m');const m=src.match(r);assert(m,'missing '+name);return m[1];}

// Execute the actual Safety_Feed body after mechanical C type/cast removal.
let code=body(safety,'Safety_Feed')
  .replace('uint32_t now','let now')
  .replace('(uint32_t)(now - phaseTick)','((now - phaseTick) >>> 0)')
  .replace(/->/g,'.').replace(/(0x[0-9A-Fa-f]+)u\b/g,'$1');
let tick=0,feeds=0;
const env={feedTick:0,phaseTick:0,phaseLimit:1000,HAL_GetTick:()=>tick,
  RESET_DEADLINE:16,Safety_Reset:()=>{throw Error('deadline');},IWDG:{}};
Object.defineProperty(env.IWDG,'KR',{set:()=>feeds++});
const ctx=vm.createContext(env),script=new vm.Script('(()=>{'+code+'})();');
const run=()=>script.runInContext(ctx);
for(let i=0;i<10000;i++)run();
check(feeds===0,'frozen initial tick must not feed');
tick=1;run();check(feeds===1,'advancing tick feeds');
for(let i=0;i<10000;i++)run();
check(feeds===1,'frozen tick cannot keep feeding');
tick=1000;assert.throws(run,/deadline/);cases++;
env.phaseTick=0xFFFFFF00;env.phaseLimit=512;tick=0xFF;run();
tick=0x100;assert.throws(run,/deadline/);cases++;

// Execute the actual work-window overlap code, retaining unsigned wrap.
let work=body(main,'workElapsed')
  .replace(/\(uint32_t\)/g,'')
  .replace(/\buint(?:8|32)_t\b/g,'let')
  .replace(/\b(\d+)u\b/g,'$1')
  .replace('let sh, eh;','let sh = windowStart, eh = windowEnd;')
  .replace('if (!timeIdxToRange(getConfigWorkStartIdx(), &sh, &eh)) return 0;','')
  .replace('beginTs = endTs - elapsed;','beginTs = (endTs - elapsed) >>> 0;');
function overlap(endTs,elapsed,start,end,synced=true){return Function('endTs','elapsed','windowStart','windowEnd','timeSynced','TIME_HOUR_END',work)(endTs,elapsed,start,end,synced,23);}
check(overlap(86400+9*3600,14*3600,8,18)===3600,'overnight only charges the working hour');
check(overlap(86400+8*3600,14*3600,8,18)===0,'work opens without overnight charge');
check(overlap(18*3600,2*3600,8,18)===7200,'working hours accumulate');
check(overlap(12*3600,1234,0,23)===1234,'full-day schedule');
check(overlap(1000,1000,8,18,false)===1000,'unsynced boot still monitored');
check(overlap(2*86400+9*3600,86400+7200,8,18)===39600,'multi-day overlap');

let timestamp=body(main,'getCurrentTimestamp')
  .replace(/\(uint32_t\)\(\(uint64_t\)localDelta \* 3600u \/\s*\(uint32_t\)\(3600 \+ rtcDriftPerHour\)\)/,
    'Number(BigInt(localDelta) * 3600n / BigInt(3600 + rtcDriftPerHour))')
  .replace(/\buint32_t\b/g,'let')
  .replace('localDelta = rtcNow - syncedRtcSec;','localDelta = (rtcNow - syncedRtcSec) >>> 0;');
check(!timestamp.includes('uint64_t'),'timestamp adapter follows actual 64-bit expression');
const timeFn=Function('rtc_read_counter','syncedTimestamp','syncedRtcSec','timeSynced','driftConverged','rtcDriftPerHour',timestamp);
for(const delta of [0,596523,596524,7*86400,11*86400,180*86400]){
  for(const drift of [-300,-30,0,30,300]){
    for(const anchor of [0,0xFFFFFF00]){
      const now=(anchor+delta)>>>0;
      const got=timeFn(()=>now,1795104000,anchor,1,1,drift);
      const want=1795104000+Number(BigInt(delta)*3600n/BigInt(3600+drift));
      check(got===want,'long-term drift and RTC wrap '+[delta,drift,anchor]);
    }
  }
}

// Power-loss oracle: any interruption must leave the old or new COMPLETE value.
// All journal/canonical writes are halfword operations; erase interruption is
// modeled at every halfword, including torn erase/program masks. The production
// C layout/commit order is checked below, rather than claiming C execution here.
const BASE=0x0800E000,PAGE=1024,JOURNAL=7*PAGE,FOOT=128,TAG=0x32564E31,COMMIT=0xA55A;
const magics=[0xA5A55A60,0xA5A55A5F,0xA5A55A5E,0xA5A55A5D,0xA5A55A5C,0x5A5AA5A6,0xA5A55A5B];
let flash,cut,steps,tearMode=false;
function crc(b){let c=0xFFFFFFFF;for(let i=0;i<84;i++){c^=i>=16&&i<20?0:b[i];for(let j=0;j<8;j++)c=(c>>>1)^((c&1)?0xEDB88320:0);}return (~c)>>>0;}
function record(key,data){const b=Buffer.alloc(88,255);b.writeUInt32LE(TAG,0);b.writeUInt32LE(BASE+key*PAGE,4);b.writeUInt32LE(magics[key],8);b.writeUInt32LE(data.length,12);data.copy(b,20);b.writeUInt32LE(crc(b),16);b.writeUInt16LE(COMMIT,84);return b;}
function valid(b){const key=(b.readUInt32LE(4)-BASE)/PAGE;return b.readUInt32LE(0)===TAG&&b.readUInt16LE(84)===COMMIT&&Number.isInteger(key)&&key>=0&&key<7&&b.readUInt32LE(8)===magics[key]&&b.readUInt32LE(12)>0&&b.readUInt32LE(12)<=64&&b.readUInt32LE(16)===crc(b);}
function bytes(r){return r.subarray(20,20+r.readUInt32LE(12));}
function tickOp(apply,tear){if(steps++===cut){if(tearMode)tear();throw Error('power');}apply();}
function erase(at){for(let n=0;n<PAGE;n+=2)tickOp(()=>flash.writeUInt16LE(0xFFFF,at+n),()=>flash.writeUInt16LE(flash.readUInt16LE(at+n)|0x5555,at+n));}
function program(at,b){for(let n=0;n<b.length;n+=2){const next=b[n]|((b[n+1]??255)<<8);tickOp(()=>{const old=flash.readUInt16LE(at+n);assert((old&next)===next,'flash cannot program 0 to 1');flash.writeUInt16LE(next,at+n);},()=>flash.writeUInt16LE(flash.readUInt16LE(at+n)&(next|0xAAAA),at+n));}}
function matches(r){const key=(r.readUInt32LE(4)-BASE)/PAGE,at=key*PAGE,f=flash.subarray(at+FOOT,at+FOOT+88);return valid(f)&&f.readUInt32LE(16)===r.readUInt32LE(16)&&flash.readUInt32LE(at)===magics[key]&&flash.subarray(at+4,at+4+bytes(r).length).equals(bytes(r));}
function restore(r){if(matches(r))return;const at=r.readUInt32LE(4)-BASE;erase(at);program(at+4,bytes(r));program(at+FOOT,r.subarray(0,84));program(at,r.subarray(8,12));program(at+FOOT+84,r.subarray(84,86));}
function recover(){const r=Buffer.from(flash.subarray(JOURNAL,JOURNAL+88));if(valid(r))restore(r);}
function save(key,data){recover();const r=record(key,data);erase(JOURNAL);program(JOURNAL,r.subarray(0,84));program(JOURNAL+84,r.subarray(84,86));restore(r);}
function load(key){const j=flash.subarray(JOURNAL,JOURNAL+88);if(valid(j)&&j.readUInt32LE(4)===BASE+key*PAGE)return Buffer.from(bytes(j));const r=flash.subarray(key*PAGE+FOOT,key*PAGE+FOOT+88);return valid(r)&&matches(r)?Buffer.from(bytes(r)):null;}
cut=Infinity;steps=0;flash=Buffer.alloc(PAGE*8,255);
const old=Buffer.from('30,0M,3t,6\0'),next=Buffer.from('60,0M,3t,6\0');
save(5,old);const baseline=Buffer.from(flash);
steps=0;save(5,next);const total=steps;
for(const torn of [false,true])for(let c=0;c<=total;c++){
  tearMode=torn;
  flash=Buffer.from(baseline);cut=c;steps=0;
  try{save(5,next);}catch(e){assert.equal(e.message,'power');}
  cut=Infinity;recover();const got=load(5);
  check(got&&(got.equals(old)||got.equals(next)),'interrupted save '+c);
  save(0,Buffer.from([0x80,0xA9,3,0]));
  check(load(5).equals(got),'next key must not destroy previous recovery '+c);
}
// Every canonical page, including the firmware marker, survives later saves.
cut=Infinity;tearMode=false;
for(let key=0;key<magics.length;key++) save(key,Buffer.from([key,0,0,0]));
for(let key=0;key<magics.length;key++) check(load(key).equals(Buffer.from([key,0,0,0])),'all seven keys '+key);
// Firmware-marker writes use the first reserved page and a four-byte payload.
cut=Infinity;tearMode=false;flash=Buffer.alloc(PAGE*8,255);
const oldVersion=Buffer.from([5,0,0,0]),newVersion=Buffer.from([6,0,0,0]);
save(0,oldVersion);const versionBaseline=Buffer.from(flash);
steps=0;save(0,newVersion);const versionSteps=steps;
for(const torn of [false,true])for(let c=0;c<=versionSteps;c++){
  tearMode=torn;flash=Buffer.from(versionBaseline);cut=c;steps=0;
  try{save(0,newVersion);}catch(e){assert.equal(e.message,'power');}
  cut=Infinity;recover();const got=load(0);
  check(got&&(got.equals(oldVersion)||got.equals(newVersion)),'interrupted version marker '+c);
}
// Recovery itself can lose power again. Keep its journal until canonical commit.
tearMode=false;flash=Buffer.from(baseline);cut=512+43+10;steps=0;try{save(5,next);}catch{}
const damaged=Buffer.from(flash);cut=Infinity;steps=0;recover();const recoverySteps=steps;
for(const torn of [false,true])for(let c=0;c<=recoverySteps;c++){
  tearMode=torn;
  flash=Buffer.from(damaged);cut=c;steps=0;try{recover();}catch(e){assert.equal(e.message,'power');}
  cut=Infinity;recover();check(load(5).equals(next),'interrupted recovery '+c);
}
// CRC catches each single-bit change to identity, length and payload.
const good=record(5,next);
for(let bit=0;bit<86*8;bit++){const b=Buffer.from(good);b[bit>>3]^=1<<(bit&7);check(!valid(b),'CRC/header/commit bit '+bit);}

// Keep model layout/commit ordering tied to the actual source and project.
check(nv.includes('#define FIRST_PAGE   0x0800E000u'),'version page belongs to persistence');
check(nv.includes('#define FOOTER       128u')&&nv.includes('#define JOURNAL_PAGE 0x0800FC00u'),'layout');
check(nv.indexOf('if (!NvStore_Recover()) return 0u;')<nv.indexOf('ok = erase(JOURNAL_PAGE)'),'recover before journal erase');
check(nv.includes('!program(r->page + FOOTER + 84u, &commit'),'canonical commit last');
check(nv.includes('program(JOURNAL_PAGE + 84u, &commit'),'journal commit last');
check(main.indexOf('  wdg_init();',main.indexOf('int main(void)'))<main.indexOf('  HAL_Init();',main.indexOf('int main(void)')),'IWDG starts before HAL');
check(main.includes('#define WDG_SEG_SEC 3u'),'sleep segment margin');
check(!/while\s*\([^)]*\)\s*\{\s*IWDG->KR/.test(main),'no direct busy-loop feeding');
check(/static volatile int packet_received/.test(read('Radio/src/pan3029_rf.c')),'ISR flag visibility');
check(read('Radio/src/pan3029_port.c').includes('SET_BIT(SPI1->CR1, SPI_CR1_SPE)'),'direct SPI enables the peripheral after HAL init');
const clockBody=body(main,'SystemClock_Config');
check(clockBody.indexOf('RCC_CFGR_SWS_PLL) return;')<clockBody.indexOf('Safety_Begin(STAGE_CLOCK'),'already-running PLL keeps the caller watchdog phase');
check(!read('MDK-ARM/STM32_ONLY_SEND.uvprojx').includes('../../Common/RadioF103/'),'project uses isolated radio');

// Fail if the checked image belongs to an older source snapshot.
const crypto=require('crypto'),manifest=JSON.parse(fs.readFileSync(path.join(root,'MDK-ARM/watchdog_build/build_manifest.json'),'utf8'));
for(const [p,hash] of Object.entries(manifest.sources))check(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,p))).digest('hex')===hash,'source manifest '+p);
for(const [p,hash] of Object.entries(manifest.outputs))check(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'MDK-ARM/watchdog_build',p))).digest('hex')===hash,'output manifest '+p);
check((read('MDK-ARM/LY-020-software/LY-020-software.sct').match(/0x0000e000/gi)||[]).length===2,'linker reserves all eight pages');
check(read('MDK-ARM/STM32_ONLY_SEND.uvprojx').includes('<Size>0xe000</Size>'),'uVision protects version page');
// Intel HEX checksum, reset vector and every programmed address are verified.
const hex=fs.readFileSync(path.join(root,'MDK-ARM/watchdog_build/STM32_ONLY_SEND.hex'),'utf8');
let upper=0,highest=0,lowest=Infinity,dataBytes=0;
const memory=new Map();
for(const line of hex.trim().split(/\r?\n/)){
  const b=Buffer.from(line.slice(1),'hex');check([...b].reduce((a,v)=>a+v,0)%256===0,'HEX checksum');
  const n=b[0],off=b.readUInt16BE(1),type=b[3];
  if(type===4)upper=b.readUInt16BE(4)*65536;
  if(type===0){const addr=upper+off;check(addr>=0x08000000&&addr+n<=BASE,'HEX avoids config and journal pages');highest=Math.max(highest,addr+n-1);lowest=Math.min(lowest,addr);dataBytes+=n;for(let i=0;i<n;i++)memory.set(addr+i,b[4+i]);}
}
const word=at=>(memory.get(at)|memory.get(at+1)<<8|memory.get(at+2)<<16|memory.get(at+3)<<24)>>>0;
check(word(0x08000000)>=0x20000000&&word(0x08000000)<=0x20005000,'initial stack in SRAM');
check((word(0x08000004)&1)===1,'Thumb reset entry');
const summary={passed:cases,powerCutSaveBoundaries:total+1,powerCutVersionBoundaries:versionSteps+1,powerCutRecoveryBoundaries:recoverySteps+1,powerCutModes:['before write','partial halfword erase/program'],programmedBytes:dataBytes,lowest:'0x'+lowest.toString(16),highest:'0x'+highest.toString(16),scope:'source-derived feed/work-time/long-term timing checks; persistence fault MODEL; HEX validation; no board execution'};
fs.writeFileSync(path.join(root,'MDK-ARM/watchdog_build/test_results.json'),JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary,null,2));
