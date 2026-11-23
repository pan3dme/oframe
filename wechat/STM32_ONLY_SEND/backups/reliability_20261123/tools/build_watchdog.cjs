/* Reproducible, non-interactive ARMCC5 build. No flashing/debugger actions. */
const fs=require('fs'),path=require('path'),cp=require('child_process'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'), mdk=path.join(root,'MDK-ARM');
const out=path.join(mdk,'watchdog_build');fs.mkdirSync(out,{recursive:true});
const bin=process.env.ARMCC5_BIN || 'D:/Keil_v5/ARM/ARM_Compiler_5.06u7/bin';
const project=fs.readFileSync(path.join(mdk,'STM32_ONLY_SEND.uvprojx'),'utf8');
const paths=[...project.matchAll(/<FilePath>([^<]+)<\/FilePath>/g)].map(m=>path.resolve(mdk,m[1]));
const inc=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/STM32F1xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32F1xx/Include','Drivers/CMSIS/Include','user','Radio/inc','../Common/inc'].map(p=>'-I'+path.resolve(root,p));
const objects=[];let logs=[];
function run(exe,args){
  const r=cp.spawnSync(path.join(bin,exe+'.exe'),args,{cwd:mdk,encoding:'utf8',windowsHide:true});
  const text=(r.stdout||'')+(r.stderr||'');logs.push(text);
  if(text.trim())console.log(text.trim());
  if(r.status!==0){fs.writeFileSync(path.join(out,'build.log'),logs.join('\n'));throw Error(exe+' failed: '+r.status+' '+(r.error||''));}
}
for(const file of paths){
  const obj=path.join(out,path.basename(file).replace(/\.[^.]+$/,'.o'));objects.push(obj);
  if(file.endsWith('.s'))run('armasm',['--cpu','Cortex-M3','--apcs=interwork','--pd','__MICROLIB SETA 1','-g','-o',obj,file]);
  else run('armcc',['-c','--c99','--cpu','Cortex-M3','--apcs=interwork','--library_type=microlib','--split_sections','-O3','-Ospace','-g','-DUSE_HAL_DRIVER','-DSTM32F103xB',...inc,'-o',obj,file]);
}
const axf=path.join(out,'STM32_ONLY_SEND.axf');
run('armlink',['--cpu','Cortex-M3','--library_type=microlib','--strict','--scatter',path.join(root,'MDK-ARM/LY-020-software/LY-020-software.sct'),'--summary_stderr','--info','summarysizes','--map','--callgraph','--info','stack','--list',path.join(out,'STM32_ONLY_SEND.map'),'-o',axf,...objects]);
run('fromelf',['--i32combined','--output',path.join(out,'STM32_ONLY_SEND.hex'),axf]);
run('fromelf',['--bin','--output',path.join(out,'STM32_ONLY_SEND.bin'),axf]);
fs.writeFileSync(path.join(out,'build.log'),logs.join('\n'));
const inputs=[...paths,'Core/Inc/reliability.h','Core/Inc/nvstore.h','MDK-ARM/STM32_ONLY_SEND.uvprojx','MDK-ARM/LY-020-software/LY-020-software.sct',...fs.readdirSync(path.join(root,'Radio/inc')).map(p=>'Radio/inc/'+p)];
const manifest={compiler:'ARMCC5',sources:{},outputs:{}};
for(const p of inputs){const abs=path.resolve(root,p);manifest.sources[path.relative(root,abs)]=crypto.createHash('sha256').update(fs.readFileSync(abs)).digest('hex');}
for(const name of ['STM32_ONLY_SEND.hex','STM32_ONLY_SEND.bin','STM32_ONLY_SEND.axf'])manifest.outputs[name]=crypto.createHash('sha256').update(fs.readFileSync(path.join(out,name))).digest('hex');
fs.writeFileSync(path.join(out,'build_manifest.json'),JSON.stringify(manifest,null,2));
console.log('BUILD OK: '+out);
