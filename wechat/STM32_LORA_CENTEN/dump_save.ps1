$enc=[Text.Encoding]::GetEncoding(936)
$L=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_LORA_CENTEN\Core\Src\main.c',$enc)
foreach($pat in @('CentenConfigRecord|CentenNameRecord|NAME_RECORD_OFF|NAME_MAGIC|NAME_COMMIT|CONFIG_MAGIC|CONFIG_COMMIT|sizeof\(CentenConfigRecord\)|static uint8_t flash_save_all|name_record_valid_at|loadNameFromFlash|configActiveAddr')){
  for($i=0;$i -lt $L.Count;$i++){
    if($L[$i] -match $pat){ '{0}: {1}' -f ($i+1), $L[$i].Trim() }
  }
  '----'
}
