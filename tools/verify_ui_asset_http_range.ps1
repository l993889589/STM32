param(
  [string]$BaseUrl = "http://127.0.0.1:8088",
  [string]$AssetFile = "D:\Embedded\H5\build\ui_assets\ui_assets.bin",
  [int]$ChunkSize = 4096,
  [switch]$Full
)

<#
  verify_ui_asset_http_range.ps1

  Purpose:
    Verify the PC-side UI asset HTTP service before the board is asked to
    download a package. The script checks manifest fields, full package CRC,
    HTTP Range response headers, per-range CRC, and sampled or full payload
    coverage.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\verify_ui_asset_http_range.ps1"
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\verify_ui_asset_http_range.ps1" -Full
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\verify_ui_asset_http_range.ps1" -ChunkSize 4096

  Constraints:
    This is a host-side gate only. It does not publish MQTT commands and does
    not write external Flash; board-side A/B commit is verified separately from
    W800 status logs. The default chunk size mirrors the production HTTP Range
    request size used by the W800 firmware.
#>

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Net.Http

function Update-Crc32 {
  param(
    [uint32]$Crc,
    [byte[]]$Data,
    [int]$Offset = 0,
    [int]$Length = -1
  )

  if($Length -lt 0) {
    $Length = $Data.Length - $Offset
  }

  $crcWork = (-bnot $Crc) -band 0xffffffff
  for($i = 0; $i -lt $Length; $i++) {
    $crcWork = $crcWork -bxor [uint32]$Data[$Offset + $i]
    for($bit = 0; $bit -lt 8; $bit++) {
      if(($crcWork -band 1) -ne 0) {
        $crcWork = (($crcWork -shr 1) -bxor 0xedb88320) -band 0xffffffff
      } else {
        $crcWork = ($crcWork -shr 1) -band 0xffffffff
      }
    }
  }

  return [uint32]((-bnot $crcWork) -band 0xffffffff)
}

function Get-FileCrc32 {
  param([string]$Path)

  $buffer = New-Object byte[] 65536
  $crc = [uint32]0
  $stream = [System.IO.File]::OpenRead($Path)
  try {
    while(($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
      $crc = Update-Crc32 -Crc $crc -Data $buffer -Offset 0 -Length $read
    }
  } finally {
    $stream.Dispose()
  }
  return $crc
}

function Invoke-RangeRequest {
  param(
    [System.Net.Http.HttpClient]$Client,
    [string]$Uri,
    [long]$Start,
    [long]$End
  )

  $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::Get, $Uri)
  [void]$request.Headers.TryAddWithoutValidation("Range", "bytes=$Start-$End")
  $response = $Client.SendAsync($request).GetAwaiter().GetResult()
  $body = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
  return [pscustomobject]@{
    Response = $response
    Body = $body
  }
}

if($ChunkSize -lt 64 -or $ChunkSize -gt 4096) {
  throw "ChunkSize must be in 64..4096 bytes for the current W800 Range path."
}

$base = $BaseUrl.TrimEnd("/")
$manifestUrl = "$base/ui/manifest.json"
$manifest = Invoke-RestMethod -Uri $manifestUrl -Method Get -TimeoutSec 10

if($manifest.magic -ne "LEDUO_UI_ASSET") {
  throw "Manifest magic mismatch: $($manifest.magic)"
}
if([uint32]$manifest.size -eq 0 -or [uint32]$manifest.version -eq 0 -or [uint32]$manifest.crc32 -eq 0) {
  throw "Manifest misses required size/version/crc32 fields."
}
if([string]::IsNullOrWhiteSpace([string]$manifest.path)) {
  throw "Manifest path is empty."
}

if(Test-Path -LiteralPath $AssetFile) {
  $assetItem = Get-Item -LiteralPath $AssetFile
  if([uint64]$assetItem.Length -ne [uint64]$manifest.size) {
    throw "Local asset size mismatch. file=$($assetItem.Length), manifest=$($manifest.size)"
  }

  $localCrc = Get-FileCrc32 -Path $assetItem.FullName
  if([uint32]$localCrc -ne [uint32]$manifest.crc32) {
    throw ("Local asset CRC mismatch. file=0x{0:X8}, manifest=0x{1:X8}" -f $localCrc, [uint32]$manifest.crc32)
  }
}

$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(15)
try {
  $assetUrl = "$base$($manifest.path)"
  $size = [long]$manifest.size
  $offsets = New-Object System.Collections.Generic.List[long]

  if($Full) {
    for($offset = 0L; $offset -lt $size; $offset += $ChunkSize) {
      $offsets.Add($offset)
    }
  } else {
    $offsets.Add(0L)
    if($size -gt ($ChunkSize * 2)) {
      $offsets.Add([long]([Math]::Floor(($size / 2) / $ChunkSize) * $ChunkSize))
    }
    if($size -gt $ChunkSize) {
      $last = [long]([Math]::Floor((($size - 1) / $ChunkSize)) * $ChunkSize)
      if(!$offsets.Contains($last)) {
        $offsets.Add($last)
      }
    }
  }

  $checkedBytes = 0L
  foreach($offset in $offsets) {
    $end = [Math]::Min($offset + $ChunkSize - 1, $size - 1)
    $range = Invoke-RangeRequest -Client $client -Uri $assetUrl -Start $offset -End $end
    $response = $range.Response
    $body = $range.Body

    if([int]$response.StatusCode -ne 206) {
      throw "Range $offset-$end returned HTTP $([int]$response.StatusCode), expected 206."
    }
    if($body.Length -ne ($end - $offset + 1)) {
      throw "Range $offset-$end length mismatch: $($body.Length)"
    }

    $rangeCrcHeader = $response.Headers.GetValues("X-Range-CRC32") | Select-Object -First 1
    $rangeCrc = Update-Crc32 -Crc 0 -Data $body
    if([uint32]$rangeCrc -ne [uint32]$rangeCrcHeader) {
      throw ("Range $offset-$end CRC mismatch. body=0x{0:X8}, header=0x{1:X8}" -f $rangeCrc, [uint32]$rangeCrcHeader)
    }

    $contentRange = $response.Content.Headers.ContentRange
    if($null -eq $contentRange -or $contentRange.From -ne $offset -or $contentRange.To -ne $end -or $contentRange.Length -ne $size) {
      throw "Range $offset-$end Content-Range mismatch: $contentRange"
    }

    $checkedBytes += $body.Length
    $response.Dispose()
  }

  [pscustomobject]@{
    Ok = $true
    BaseUrl = $base
    Version = [uint32]$manifest.version
    Size = [uint32]$manifest.size
    Crc32 = ("0x{0:X8}" -f [uint32]$manifest.crc32)
    ChunkSize = $ChunkSize
    Mode = $(if($Full) { "full" } else { "sample" })
    RangesChecked = $offsets.Count
    BytesChecked = $checkedBytes
  } | Format-List
} finally {
  $client.Dispose()
}
