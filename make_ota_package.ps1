param(
  [uint32]$ImageVersion = 1,
  [string]$InputFile = "",
  [uint32]$AppBase = 0x08020000,
  [string]$PackageName = "",
  [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$FromElf = "C:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe"
$AppAxf = Join-Path $Root "STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.axf"

if ([string]::IsNullOrWhiteSpace($InputFile)) {
  $InputFile = $AppAxf
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $Root "ota_package"
}

if ([string]::IsNullOrWhiteSpace($PackageName)) {
  $AppBin = Join-Path $OutputDir "app.bin"
  $ManifestBin = Join-Path $OutputDir "manifest.bin"
  $ManifestJson = Join-Path $OutputDir "manifest.json"
} else {
  $SafePackageName = $PackageName -replace '[<>:"/\\|?*]', ''
  $AppBin = Join-Path $OutputDir "$SafePackageName.bin"
  $ManifestBin = Join-Path $OutputDir "$SafePackageName.manifest.bin"
  $ManifestJson = Join-Path $OutputDir "$SafePackageName.manifest.json"
}

$OTA_MANIFEST_MAGIC = [uint32]0x4F54414D
$OTA_MANIFEST_VERSION = [uint32]1
$OTA_BOOT_STATE_PENDING_UPDATE = [uint32]1
$OTA_APP_BASE = [uint32]$AppBase
$OTA_EXT_DOWNLOAD_ADDR = [uint32]0x00010000
$MANIFEST_SIZE = 188

function Update-Crc32 {
  param(
    [uint32]$Crc,
    [byte[]]$Data,
    [int]$Length = -1
  )

  if ($Length -lt 0) {
    $Length = $Data.Length
  }

  $crcWork = -bnot $Crc
  for ($i = 0; $i -lt $Length; $i++) {
    $crcWork = $crcWork -bxor $Data[$i]
    for ($bit = 0; $bit -lt 8; $bit++) {
      if (($crcWork -band 1) -ne 0) {
        $crcWork = (($crcWork -shr 1) -bxor 0xEDB88320)
      } else {
        $crcWork = ($crcWork -shr 1)
      }
    }
  }

  return [uint32](-bnot $crcWork)
}

function Write-U32 {
  param(
    [byte[]]$Buffer,
    [int]$Offset,
    [uint32]$Value
  )

  $bytes = [BitConverter]::GetBytes($Value)
  [Array]::Copy($bytes, 0, $Buffer, $Offset, 4)
}

function Convert-BytesToHex {
  param([byte[]]$Bytes)

  return (($Bytes | ForEach-Object { $_.ToString("X2") }) -join "")
}

function Convert-IntelHexToBin {
  param(
    [string]$HexPath,
    [uint32]$BaseAddress
  )

  $records = New-Object 'System.Collections.Generic.List[object]'
  $upper = [uint32]0
  $min = [uint64]::MaxValue
  $max = [uint64]0
  $lineNo = 0

  foreach ($lineRaw in [System.IO.File]::ReadLines($HexPath)) {
    $lineNo++
    $line = $lineRaw.Trim()
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    if (!$line.StartsWith(":")) {
      throw "Invalid HEX line ${lineNo}: missing ':'"
    }

    $byteCount = [Convert]::ToByte($line.Substring(1, 2), 16)
    $offset = [Convert]::ToUInt16($line.Substring(3, 4), 16)
    $recordType = [Convert]::ToByte($line.Substring(7, 2), 16)
    $data = New-Object byte[] $byteCount

    for ($i = 0; $i -lt $byteCount; $i++) {
      $data[$i] = [Convert]::ToByte($line.Substring(9 + ($i * 2), 2), 16)
    }

    $sum = [uint32]$byteCount + (($offset -shr 8) -band 0xFF) + ($offset -band 0xFF) + $recordType
    foreach ($b in $data) {
      $sum += $b
    }
    $checksum = [Convert]::ToByte($line.Substring(9 + ($byteCount * 2), 2), 16)
    if ((($sum + $checksum) -band 0xFF) -ne 0) {
      throw "Invalid HEX checksum at line $lineNo"
    }

    switch ($recordType) {
      0 {
        $address = [uint64]($upper + $offset)
        if ($address -ge $BaseAddress) {
          $records.Add([pscustomobject]@{ Address = $address; Data = $data }) | Out-Null
          if ($address -lt $min) { $min = $address }
          $end = $address + [uint64]$data.Length
          if ($end -gt $max) { $max = $end }
        }
      }
      1 { break }
      2 {
        if ($byteCount -ne 2) { throw "Invalid HEX segment address at line $lineNo" }
        $upper = [uint32](([uint32]$data[0] -shl 12) -bor ([uint32]$data[1] -shl 4))
      }
      4 {
        if ($byteCount -ne 2) { throw "Invalid HEX linear address at line $lineNo" }
        $upper = [uint32](([uint32]$data[0] -shl 24) -bor ([uint32]$data[1] -shl 16))
      }
      default {}
    }
  }

  if ($records.Count -eq 0) {
    throw "No HEX data found at or above AppBase 0x{0:X8}" -f $BaseAddress
  }

  if ($min -gt $BaseAddress) {
    throw "HEX starts at 0x{0:X8}, expected AppBase 0x{1:X8}" -f $min, $BaseAddress
  }

  $image = New-Object byte[] ([int]($max - [uint64]$BaseAddress))
  for ($i = 0; $i -lt $image.Length; $i++) {
    $image[$i] = 0xFF
  }

  foreach ($record in $records) {
    $dst = [int]([uint64]$record.Address - [uint64]$BaseAddress)
    [Array]::Copy($record.Data, 0, $image, $dst, $record.Data.Length)
  }

  return $image
}

if (!(Test-Path -LiteralPath $InputFile)) {
  throw "Input file not found: $InputFile. Build the app first."
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$ext = [System.IO.Path]::GetExtension($InputFile).ToLowerInvariant()

Write-Host "Creating app.bin from $InputFile..."
switch ($ext) {
  ".axf" {
    if (!(Test-Path -LiteralPath $FromElf)) {
      throw "fromelf.exe not found: $FromElf"
    }
    & $FromElf --bin --output $AppBin $InputFile
    if ($LASTEXITCODE -ne 0) {
      throw "fromelf failed, exit code $LASTEXITCODE"
    }
  }
  ".elf" {
    if (!(Test-Path -LiteralPath $FromElf)) {
      throw "fromelf.exe not found: $FromElf"
    }
    & $FromElf --bin --output $AppBin $InputFile
    if ($LASTEXITCODE -ne 0) {
      throw "fromelf failed, exit code $LASTEXITCODE"
    }
  }
  ".hex" {
    $hexImage = Convert-IntelHexToBin -HexPath $InputFile -BaseAddress $OTA_APP_BASE
    [System.IO.File]::WriteAllBytes($AppBin, $hexImage)
  }
  ".bin" {
    Copy-Item -LiteralPath $InputFile -Destination $AppBin -Force
  }
  default {
    throw "Unsupported input file type '$ext'. Use .axf, .elf, .hex, or .bin."
  }
}

$image = [System.IO.File]::ReadAllBytes($AppBin)
if ($image.Length -lt 8) {
  throw "App image is too small: $($image.Length) bytes"
}
$imageSize = [uint32]$image.Length
$imageCrc = Update-Crc32 -Crc 0 -Data $image
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
  $imageSha = $sha256.ComputeHash($image)
} finally {
  $sha256.Dispose()
}
$resetVector = [BitConverter]::ToUInt32($image, 4)

$manifest = New-Object byte[] $MANIFEST_SIZE
Write-U32 $manifest 0  $OTA_MANIFEST_MAGIC
Write-U32 $manifest 4  $OTA_MANIFEST_VERSION
Write-U32 $manifest 8  $OTA_BOOT_STATE_PENDING_UPDATE
Write-U32 $manifest 12 $imageSize
Write-U32 $manifest 16 $ImageVersion
Write-U32 $manifest 20 0
Write-U32 $manifest 24 $imageCrc
Write-U32 $manifest 28 $OTA_EXT_DOWNLOAD_ADDR
Write-U32 $manifest 32 $imageSize
Write-U32 $manifest 36 0
Write-U32 $manifest 40 0
Write-U32 $manifest 44 0
Write-U32 $manifest 48 $OTA_APP_BASE
Write-U32 $manifest 52 $resetVector
[Array]::Copy($imageSha, 0, $manifest, 56, 32)
[Array]::Copy($imageSha, 0, $manifest, 88, 32)
$manifestCrc = Update-Crc32 -Crc 0 -Data $manifest
Write-U32 $manifest 184 $manifestCrc

[System.IO.File]::WriteAllBytes($ManifestBin, $manifest)

$summary = [ordered]@{
  magic = "0x{0:X8}" -f $OTA_MANIFEST_MAGIC
  version = $OTA_MANIFEST_VERSION
  boot_state = "PENDING_UPDATE"
  image_version = $ImageVersion
  image_size = $imageSize
  image_crc32 = "0x{0:X8}" -f $imageCrc
  package_address = "0x{0:X8}" -f $OTA_EXT_DOWNLOAD_ADDR
  package_size = $imageSize
  rollback_address = "0x00000000"
  rollback_size = 0
  rollback_crc32 = "0x00000000"
  load_address = "0x{0:X8}" -f $OTA_APP_BASE
  entry_address = "0x{0:X8}" -f $resetVector
  image_sha256 = Convert-BytesToHex $imageSha
  manifest_crc32 = "0x{0:X8}" -f $manifestCrc
  app_bin = $AppBin
  manifest_bin = $ManifestBin
}

$json = $summary | ConvertTo-Json
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($ManifestJson, $json, $utf8NoBom)
$summary | Format-List
