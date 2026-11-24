$enc=[Text.Encoding]::GetEncoding(936)
$L=[IO.File]::ReadAllLines('d:\oframesrc\wechat\STM32_LORA_CENTEN\Core\Src\main.c',$enc)
'==== record defs 180-215 ===='
for($i=179;$i -le 214;$i++){ '{0}: {1}' -f ($i+1), $L[$i] }
'==== flash_save_all 855-915 ===='
for($i=854;$i -le 914;$i++){ '{0}: {1}' -f ($i+1), $L[$i] }
'==== loadNameFromFlash 945-965 ===='
for($i=944;$i -le 964;$i++){ '{0}: {1}' -f ($i+1), $L[$i] }
'==== name fns 828-854 ===='
for($i=827;$i -le 853;$i++){ '{0}: {1}' -f ($i+1), $L[$i] }
