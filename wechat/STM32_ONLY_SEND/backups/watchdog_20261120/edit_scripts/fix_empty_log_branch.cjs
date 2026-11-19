const fs=require('fs');const p='Core/Src/main.c';
let s=fs.readFileSync(p).toString('latin1');
if(!s.includes('    else\r\n  } else {'))throw Error('missing branch');
s=s.replace('    else\r\n  } else {','  } else {');
fs.writeFileSync(p,Buffer.from(s,'latin1'));
