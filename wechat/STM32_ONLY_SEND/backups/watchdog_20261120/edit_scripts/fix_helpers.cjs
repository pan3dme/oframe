const fs=require('fs');
let s=fs.readFileSync('Core/Src/main.c').toString('latin1');
s=s.replace('return twoCharToIndex(f) >= 0 && twoCharToIndex(f) <= 275; /*', 'return 1; /*');
const re=/static uint8_t configTimeFieldValid\(const char \*f\)\r?\n\{[\s\S]*?^\}/m;
if(!re.test(s))throw Error('missing helper');
s=s.replace(re,b=>b.replace('  return 1;', '  return twoCharToIndex(f) >= 0 && twoCharToIndex(f) <= 275;'));
fs.writeFileSync('Core/Src/main.c',Buffer.from(s,'latin1'));
