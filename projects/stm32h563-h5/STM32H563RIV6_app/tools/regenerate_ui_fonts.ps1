<#
  Rebuild the four CJK fonts used by the product UI from the actual Chinese
  characters present in sim_ui.c. ASCII remains available for diagnostics.
#>
param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$UiSource = Join-Path $Root "user\ui\lvgl\sim_ui.c"
$FontFile = "C:\Windows\Fonts\simhei.ttf"
$Converter = (Get-Command lv_font_conv -ErrorAction Stop).Source

if(!(Test-Path -LiteralPath $FontFile)) {
  throw "SimHei font not found: $FontFile"
}

$characters = [System.Collections.Generic.SortedSet[int]]::new()
$sourceText = Get-Content -Raw -Encoding UTF8 -LiteralPath $UiSource
foreach($character in $sourceText.ToCharArray()) {
  $codepoint = [int]$character
  if($codepoint -gt 0x7E) {
    [void]$characters.Add($codepoint)
  }
}
$ranges = @("0x20-0x7E") + @($characters | ForEach-Object { "0x{0:X}" -f $_ })
$rangeArgument = $ranges -join ","

foreach($size in @(12, 14, 18, 20)) {
  $output = Join-Path $Root "user\ui\lvgl\font_cjk_$size.c"
  & $Converter `
      --font $FontFile `
      --range $rangeArgument `
      --size $size `
      --bpp 4 `
      --format lvgl `
      --lv-include "lvgl.h" `
      --output $output `
      --lv-font-name "lv_font_cjk_$size" `
      --force-fast-kern-format `
      --no-compress
  if($LASTEXITCODE -ne 0) {
    throw "lv_font_conv failed for ${size}px"
  }
}

Write-Host "Regenerated product UI fonts for $($characters.Count) non-ASCII characters."
