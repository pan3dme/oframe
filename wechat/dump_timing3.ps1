$enc=[Text.Encoding]::GetEncoding(936)
$C=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_LORA_CENTEN\Core\Src\main.c',$enc)
'==== CENTEN delay constants ===='
for($i=0;$i -lt $C.Count;$i++){
  if($C[$i] -match 'LORA_SYNCTIME_DELAY_MS|LORA_CMD_DELAY_MS|DEFAULT_SF|#define DEFAULT_FREQ'){ '{0}: {1}' -f ($i+1), $C[$i].Trim() }
}
'----'
$O=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_ONLY_SEND\Core\Src\main.c',$enc)
'==== ONLY_SEND openRxWindow callers context 2780-2800 ===='
for($i=2779;$i -le 2799;$i++){ '{0}: {1}' -f ($i+1), $O[$i] }
'==== 2880-2900 ===='
for($i=2879;$i -le 2899;$i++){ '{0}: {1}' -f ($i+1), $O[$i] }
'==== ONLY_SEND DEFAULT_SF / air time ===='
for($i=0;$i -lt $O.Count;$i++){
  if($O[$i] -match '#define DEFAULT_SF|#define DEFAULT_FREQ'){ '{0}: {1}' -f ($i+1), $O[$i].Trim() }
}
