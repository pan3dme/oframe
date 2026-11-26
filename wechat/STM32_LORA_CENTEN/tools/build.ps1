param([string]$CompilerBin = '', [switch]$TestImage)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$mdkRoot = Join-Path $projectRoot 'MDK-ARM'
$buildRoot = Join-Path $projectRoot 'build'
if ($TestImage) { $buildRoot = Join-Path $buildRoot 'test-image' }
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
[xml]$projectXml = Get-Content -LiteralPath (Join-Path $mdkRoot 'STM32_LORA_CENTEN.uvprojx') -Raw
$useAc6 = [string]$projectXml.Project.Targets.Target.uAC6 -eq '1'
if (!$CompilerBin) {
    $CompilerBin = if ($useAc6) { 'D:\Keil_v5\ARM\ARMCLANG\Bin' } else { 'D:\Keil_v5\ARM\ARM_Compiler_5.06u7\Bin' }
}
$includeDirs = @('Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/STM32F1xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32F1xx/Include','Drivers/CMSIS/Include','user') | ForEach-Object { Join-Path $projectRoot $_ }
$includeDirs += @((Join-Path $projectRoot 'Common/RadioF103/inc'), (Join-Path $projectRoot 'Common/inc'))
$compileArgs = @('--cpu','Cortex-M3','--c99','-O2','-Ospace','--apcs=interwork','--library_type=microlib','-g','--split_sections','-D__MICROLIB','-DUSE_HAL_DRIVER','-DSTM32F103xB')
$compiler = 'armcc.exe'
if ($useAc6) {
    $compiler = 'armclang.exe'
    $compileArgs = @('--target=arm-arm-none-eabi','-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-std=c99','-Oz','-g','-ffunction-sections','-fdata-sections','-fshort-enums','-fshort-wchar','-D__MICROLIB','-DUSE_HAL_DRIVER','-DSTM32F103xB','-Wno-invalid-source-encoding')
    if ($TestImage) { $compileArgs += '-fno-inline-functions' }
}
foreach ($includeDir in $includeDirs) { $compileArgs += @('-I', $includeDir) }
$objects = @()
$log = New-Object System.Collections.Generic.List[string]
foreach ($file in $projectXml.Project.Targets.Target.Groups.Group.Files.File) {
    if ($file.FileType -ne '1' -and $file.FileType -ne '2') { continue }
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $mdkRoot ([string]$file.FilePath)))
    $objectPath = Join-Path $buildRoot (([IO.Path]::GetFileNameWithoutExtension($sourcePath)) + '.o')
    if ($file.FileType -eq '2') {
        $output = & (Join-Path $CompilerBin 'armasm.exe') '--cpu' 'Cortex-M3' '--apcs=interwork' '--pd' '__MICROLIB SETA 1' '-g' '-o' $objectPath $sourcePath 2>&1
    } else {
        $output = & (Join-Path $CompilerBin $compiler) @compileArgs '-c' '-o' $objectPath $sourcePath 2>&1
    }
    $output | ForEach-Object { $log.Add([string]$_); Write-Output $_ }
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $sourcePath" }
    $objects += $objectPath
}
$scatterPath = Join-Path $buildRoot 'CENTEN.sct'
@'
LR_IROM1 0x08000000 0x0000F800 {
  ER_IROM1 0x08000000 0x0000F800 {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
    .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x00005000 { .ANY (+RW +ZI) }
}
'@ | Set-Content -LiteralPath $scatterPath -Encoding ascii
$output = & (Join-Path $CompilerBin 'armlink.exe') '--cpu' 'Cortex-M3' @objects '--library_type=microlib' '--strict' '--scatter' $scatterPath '--summary_stderr' '--info' 'summarysizes' '--map' '--xref' '--callgraph' '--symbols' '--list' (Join-Path $buildRoot 'CENTEN.map') '-o' (Join-Path $buildRoot 'CENTEN.axf') 2>&1
$output | ForEach-Object { $log.Add([string]$_); Write-Output $_ }
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
& (Join-Path $CompilerBin 'fromelf.exe') '--i32combined' '--output' (Join-Path $buildRoot 'CENTEN.hex') (Join-Path $buildRoot 'CENTEN.axf')
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
$log | Set-Content -LiteralPath (Join-Path $buildRoot 'build.log') -Encoding utf8
Write-Output 'CENTEN build completed.'
