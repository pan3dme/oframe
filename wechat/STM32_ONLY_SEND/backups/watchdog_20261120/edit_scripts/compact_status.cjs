const fs=require('fs');
let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
const re=/^static void gpsPrintSearchStatus\(uint32_t elapsedMs\)\r?\n\{[\s\S]*?^\}/m;
if(!re.test(s))throw Error('Missing GPS status');
s=s.replace(re,`static void gpsPrintSearchStatus(uint32_t elapsedMs)
{
  char rtcBuf[24], latBuf[16], lngBuf[16];
  formatTimestamp(getCurrentTimestamp(), rtcBuf, sizeof(rtcBuf));
  deg1e5ToStr(gpsFix.lat, latBuf);
  deg1e5ToStr(gpsFix.lng, lngBuf);
  printf("[GPS] fix=%u %s,%s sat=%u t=%lus rx=%lu RTC=%s\\r\\n",
         (unsigned)gpsFixFresh(), latBuf, lngBuf, (unsigned)gpsFix.sats,
         (unsigned long)(elapsedMs / 1000u), (unsigned long)gpsRxCount, rtcBuf);
}`.replace(/\n/g,'\r\n'));
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
