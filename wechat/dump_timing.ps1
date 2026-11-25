$enc=[Text.Encoding]::GetEncoding(936)
$C=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_LORA_CENTEN\Core\Src\main.c',$enc)
'==== CENTEN job scheduling (loraJobs due) ===='
for($i=0;$i -lt $C.Count;$i++){
  if($C[$i] -match 'loraJobs\[.*\]\.due|scheduleLoraReply|scheduleLoraJob|\.due =|WAKE_DELAY|wake.*delay|reply.*delay'){ '{0}: {1}' -f ($i+1), $C[$i].Trim() }
}
'----'
$O=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_ONLY_SEND\Core\Src\main.c',$enc)
'==== ONLY_SEND RX window ===='
for($i=0;$i -lt $O.Count;$i++){
  if($O[$i] -match 'RX_WINDOW|rxWindow|rx_window|STAGE_RX|enter.*rx|RADIO_FLAG_RXDONE|3000|3s|listen'){ '{0}: {1}' -f ($i+1), $O[$i].Trim() }
}
