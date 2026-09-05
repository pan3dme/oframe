$ErrorActionPreference = 'SilentlyContinue'
$root = 'd:\oframesrc\cowsheepwechat'
$exts = @('*.js', '*.wxml')
$dirs = @('pages', 'config', 'utils', 'components')
$files = @()
foreach ($d in $dirs) {
  foreach ($e in $exts) {
    $files += Get-ChildItem -Path (Join-Path $root $d) -Recurse -Filter $e
  }
}
$pattern = $args[0]
foreach ($f in $files) {
  $content = Get-Content -LiteralPath $f.FullName -Encoding UTF8
  for ($i = 0; $i -lt $content.Count; $i++) {
    $line = $content[$i]
    if ($line -match $pattern) {
      $rel = $f.FullName.Substring($root.Length + 1)
      Write-Output ("{0}:{1}: {2}" -f $rel, ($i + 1), $line.Trim())
    }
  }
}
