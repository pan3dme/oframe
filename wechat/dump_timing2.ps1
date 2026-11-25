$enc=[Text.Encoding]::GetEncoding(936)
$C=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_LORA_CENTEN\Core\Src\main.c',$enc)
'==== CENTEN scheduleLoraReply 396-418 ===='
for($i=395;$i -le 417;$i++){ '{0}: {1}' -f ($i+1), $C[$i] }
'==== CENTEN rx handler 520-545 ===='
for($i=519;$i -le 544;$i++){ '{0}: {1}' -f ($i+1), $C[$i] }
'==== CENTEN tx time estimate / air time ===='
for($i=0;$i -lt $C.Count;$i++){
  if($C[$i] -match 'centen_command_frame\s*\(' -and $C[$i] -match 'static'){ '{0}: {1}' -f ($i+1), $C[$i].Trim() }
}
'----'
$O=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_ONLY_SEND\Core\Src\main.c',$enc)
'==== ONLY_SEND openRxWindow3000 2634-2660 ===='
for($i=2633;$i -le 2659;$i++){ '{0}: {1}' -f ($i+1), $O[$i] }
'==== ONLY_SEND RX done handling 2645-2700 ===='
for($i=2644;$i -le 2699;$i++){
  if($O[$i] -match 'RADIO_FLAG_RXDONE|RXERR|rf_get_recv_flag|openRx|process|recv_flag'){ '{0}: {1}' -f ($i+1), $O[$i].Trim() }
}
'==== ONLY_SEND callers of openRxWindow3000 (context) ===='
for($i=0;$i -lt $O.Count;$i++){
  if($O[$i] -match 'openRxWindow3000\(\);'){ '{0}: {1}' -f ($i+1), $O[$i].Trim() }
}
