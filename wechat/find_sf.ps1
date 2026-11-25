foreach($p in @('d:\oframesrc\wechat\STM32_ONLY_SEND\Radio\inc\radio.h',
                'd:\oframesrc\wechat\STM32_LORA_CENTEN\Common\RadioF103\inc\radio.h')){
  if(Test-Path $p){
    '== '+$p
    Get-Content $p -Encoding ([Text.Encoding]::GetEncoding(936)) | Select-String -Pattern 'DEFAULT_SF|DEFAULT_FREQ|DEFAULT_BW' | ForEach-Object { $_.Line.Trim() }
  }
}
