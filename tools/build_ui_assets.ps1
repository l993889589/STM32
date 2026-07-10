param(
    [switch]$ClearStaticValues,
    [switch]$KeepStaticValues
)

<#
  build_ui_assets.ps1

  Purpose:
    Build the external-Flash UI asset package from the reference images under
    D:\Embedded\H5\pic. The default mode clears static numeric/text values from
    dynamic data regions so the firmware overlay can redraw live values without
    ghosting over values baked into the reference pictures.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\build_ui_assets.ps1"
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\build_ui_assets.ps1" -ClearStaticValues
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\build_ui_assets.ps1" -KeepStaticValues

  Constraints:
    The output format is a fixed 480x320 RGB565 page table package consumed by
    ui_asset_store.c. KeepStaticValues is only for visual comparison against the
    original reference pictures; production and HTTP update tests should use
    masked dynamic zones.
#>

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$PicDir = Join-Path $Root "pic"
$OutDir = Join-Path $Root "build\ui_assets"
$OutBin = Join-Path $OutDir "ui_assets.bin"
$OutManifest = Join-Path $OutDir "ui_assets_manifest.json"

$AssetVersion = if($env:UI_ASSET_VERSION) { [uint32]$env:UI_ASSET_VERSION } else { [uint32]2026070601 }
$ClearStaticValuesEnabled = ![bool]$KeepStaticValues
if($ClearStaticValues) {
    $ClearStaticValuesEnabled = $true
}
if($env:UI_ASSET_CLEAR_STATIC) {
    $ClearStaticValuesEnabled = @("1", "true", "yes", "on") -contains $env:UI_ASSET_CLEAR_STATIC.ToLowerInvariant()
}
$AssetMode = if($ClearStaticValuesEnabled) { "masked" } else { "original" }

$Width = 480
$Height = 320
$HeaderSize = 256
$EntrySize = 32
$Align = 4096
$Magic = [Convert]::ToUInt32("50414955", 16)
$SchemaVersion = [uint32]1
$FormatRgb565 = [uint32]1
$ValidFlag = [Convert]::ToUInt32("A55A5AA5", 16)
$SlotSize = [uint32]0x00500000

$PageList = @(
    @{ Id = 0; Name = "brand";   File = "beb0ed1df7c3de72abb4b0fd4229415a.png" },
    @{ Id = 1; Name = "monitor"; File = "70afbaff6c736f0d3a66b35c82c2bc38.png" },
    @{ Id = 2; Name = "sensor";  File = "808cb614ff3d111885ed7371d5d79891.png" },
    @{ Id = 3; Name = "event";   File = "5c3e140423c1eddbbb1ce1f46d3a637e.png" },
    @{ Id = 4; Name = "comm";    File = "10ee8bfe01457ec5cec32803e1811569.png"; Crop = @(68, 93, 890, 593) }
)

foreach($Page in $PageList) {
    $FullPath = Join-Path $PicDir $Page.File
    if(!(Test-Path -LiteralPath $FullPath)) {
        throw "Missing reference image: $FullPath"
    }
    $Page["FullPath"] = $FullPath
}

function Update-Crc32([uint32]$Crc, [byte[]]$Data, [int]$Offset, [int]$Length) {
    $Crc = -bnot $Crc
    for($I = 0; $I -lt $Length; $I++) {
        $Crc = $Crc -bxor $Data[$Offset + $I]
        for($Bit = 0; $Bit -lt 8; $Bit++) {
            if(($Crc -band 1) -ne 0) {
                $Crc = ($Crc -shr 1) -bxor [Convert]::ToUInt32("EDB88320", 16)
            }
            else {
                $Crc = $Crc -shr 1
            }
        }
    }
    return [uint32](-bnot $Crc)
}

function Get-Crc32([byte[]]$Data) {
    return Update-Crc32 0 $Data 0 $Data.Length
}

function Align-Up([uint32]$Value, [uint32]$Alignment) {
    return [uint32]((($Value + $Alignment - 1) / $Alignment) -as [uint32]) * $Alignment
}

function Fill-Rect([System.Drawing.Bitmap]$Bmp, [int]$X, [int]$Y, [int]$W, [int]$H, [string]$ColorHex) {
    $ColorValue = [Convert]::ToInt32($ColorHex.TrimStart("#"), 16)
    $Color = [System.Drawing.Color]::FromArgb(
        ($ColorValue -shr 16) -band 0xFF,
        ($ColorValue -shr 8) -band 0xFF,
        $ColorValue -band 0xFF)
    $G = [System.Drawing.Graphics]::FromImage($Bmp)
    $Brush = New-Object System.Drawing.SolidBrush $Color
    $G.FillRectangle($Brush, $X, $Y, $W, $H)
    $Brush.Dispose()
    $G.Dispose()
}

function Clear-StaticValues([System.Drawing.Bitmap]$Bmp, [string]$PageId) {
    if($PageId -eq "monitor") {
        Fill-Rect $Bmp 388 18 76 22 "#101C2B"
        Fill-Rect $Bmp 60 123 78 35 "#0E2246"
        Fill-Rect $Bmp 154 123 70 36 "#10243A"
        Fill-Rect $Bmp 292 123 82 35 "#0D2D3A"
        Fill-Rect $Bmp 384 123 72 36 "#10263C"
        Fill-Rect $Bmp 78 260 88 34 "#10233D"
        Fill-Rect $Bmp 230 260 82 34 "#102A35"
        Fill-Rect $Bmp 389 260 58 34 "#221E1A"
    }
    elseif($PageId -eq "sensor") {
        Fill-Rect $Bmp 68 116 76 40 "#112A46"
        Fill-Rect $Bmp 222 116 76 40 "#112A46"
        Fill-Rect $Bmp 369 116 80 40 "#112A46"
        Fill-Rect $Bmp 388 18 76 22 "#0D1D33"
    }
    elseif($PageId -eq "comm") {
        Fill-Rect $Bmp 356 30 83 26 "#111B27"
        Fill-Rect $Bmp 38 99 86 28 "#20734D"
        Fill-Rect $Bmp 176 99 60 28 "#7A4A20"
        Fill-Rect $Bmp 318 99 76 28 "#813B62"
        Fill-Rect $Bmp 44 154 116 58 "#122D38"
        Fill-Rect $Bmp 350 158 94 52 "#162C3A"
        Fill-Rect $Bmp 354 226 70 38 "#162C3A"
    }
}

function Convert-ToRgb565Map([System.Drawing.Bitmap]$Bmp) {
    $Pixels = New-Object byte[] ($Bmp.Width * $Bmp.Height * 2)
    $Out = 0
    for($Y = 0; $Y -lt $Bmp.Height; $Y++) {
        for($X = 0; $X -lt $Bmp.Width; $X++) {
            $C = $Bmp.GetPixel($X, $Y)
            $V = (($C.R -band 0xF8) -shl 8) -bor (($C.G -band 0xFC) -shl 3) -bor ($C.B -shr 3)
            $Pixels[$Out] = [byte]($V -band 0xFF)
            $Pixels[$Out + 1] = [byte](($V -shr 8) -band 0xFF)
            $Out += 2
        }
    }
    return ,$Pixels
}

function New-PageBitmap($Page) {
    $Src = [System.Drawing.Image]::FromFile($Page.FullPath)
    $Bmp = New-Object System.Drawing.Bitmap $Width, $Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $G = [System.Drawing.Graphics]::FromImage($Bmp)
    $G.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $G.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $G.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    if($Page.ContainsKey("Crop")) {
        $Crop = $Page.Crop
        $Dst = New-Object System.Drawing.Rectangle 0, 0, $Width, $Height
        $G.DrawImage($Src, $Dst, $Crop[0], $Crop[1], $Crop[2], $Crop[3],
                     [System.Drawing.GraphicsUnit]::Pixel)
    }
    else {
        $G.DrawImage($Src, 0, 0, $Width, $Height)
    }
    $G.Dispose()
    $Src.Dispose()
    if($ClearStaticValuesEnabled) {
        Clear-StaticValues $Bmp $Page.Name
    }
    return $Bmp
}

function Write-U32([System.IO.BinaryWriter]$Writer, [uint32]$Value) {
    $Writer.Write([byte]($Value -band 0xFF))
    $Writer.Write([byte](($Value -shr 8) -band 0xFF))
    $Writer.Write([byte](($Value -shr 16) -band 0xFF))
    $Writer.Write([byte](($Value -shr 24) -band 0xFF))
}

function Set-U32([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    $Buffer[$Offset + 0] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Images = @()
$DataOffset = Align-Up ([uint32]($HeaderSize + ($EntrySize * $PageList.Count))) $Align
foreach($Page in $PageList) {
    $Bmp = New-PageBitmap $Page
    $Data = Convert-ToRgb565Map $Bmp
    $Bmp.Dispose()
    $Images += [PSCustomObject]@{
        Id = [uint32]$Page.Id
        Name = $Page.Name
        File = $Page.File
        Offset = [uint32]$DataOffset
        Size = [uint32]$Data.Length
        Crc32 = Get-Crc32 $Data
        Data = $Data
    }
    $DataOffset = Align-Up ([uint32]($DataOffset + $Data.Length)) $Align
}

$TotalSize = [uint32]$DataOffset
if($TotalSize -gt $SlotSize) {
    throw "Asset package $TotalSize exceeds slot size $SlotSize"
}

$Header = New-Object byte[] $HeaderSize
$Table = New-Object byte[] ($EntrySize * $Images.Count)

for($I = 0; $I -lt $Images.Count; $I++) {
    $E = $Images[$I]
    $Base = $I * $EntrySize
    Set-U32 $Table ($Base + 0) $E.Id
    Set-U32 $Table ($Base + 4) $FormatRgb565
    Set-U32 $Table ($Base + 8) ([uint32]$Width)
    Set-U32 $Table ($Base + 12) ([uint32]$Height)
    Set-U32 $Table ($Base + 16) $E.Offset
    Set-U32 $Table ($Base + 20) $E.Size
    Set-U32 $Table ($Base + 24) ([uint32]($Width * 2))
    Set-U32 $Table ($Base + 28) $E.Crc32
}

$DataCrc = 0
foreach($E in $Images) {
    $DataCrc = Update-Crc32 $DataCrc $E.Data 0 $E.Data.Length
}
$TableCrc = Get-Crc32 $Table

Set-U32 $Header 0 $Magic
Set-U32 $Header 4 $SchemaVersion
Set-U32 $Header 8 ([uint32]$HeaderSize)
Set-U32 $Header 12 ([uint32]$EntrySize)
Set-U32 $Header 16 ([uint32]$Images.Count)
Set-U32 $Header 20 $TotalSize
Set-U32 $Header 24 $AssetVersion
Set-U32 $Header 28 ([uint32]$Width)
Set-U32 $Header 32 ([uint32]$Height)
Set-U32 $Header 36 $SlotSize
Set-U32 $Header 40 $ValidFlag
Set-U32 $Header 44 ([uint32]$HeaderSize)
Set-U32 $Header 48 $TableCrc
Set-U32 $Header 52 $DataCrc

$Fs = [System.IO.File]::Open($OutBin, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
    $Writer = [System.IO.BinaryWriter]::new($Fs)
    $Writer.Write($Header)
    $Writer.Write($Table)
    while($Fs.Position -lt $Images[0].Offset) {
        $Writer.Write([byte]0xFF)
    }
    foreach($E in $Images) {
        while($Fs.Position -lt $E.Offset) {
            $Writer.Write([byte]0xFF)
        }
        $Writer.Write($E.Data)
    }
    while($Fs.Position -lt $TotalSize) {
        $Writer.Write([byte]0xFF)
    }
}
finally {
    $Fs.Dispose()
}

$Manifest = [PSCustomObject]@{
    magic = "UIAP"
    version = $AssetVersion
    totalSize = $TotalSize
    slotSize = $SlotSize
    imageCount = $Images.Count
    sourceMode = $AssetMode
    width = $Width
    height = $Height
    tableCrc32 = ("0x{0:X8}" -f $TableCrc)
    dataCrc32 = ("0x{0:X8}" -f $DataCrc)
    images = @($Images | ForEach-Object {
        [PSCustomObject]@{
            id = $_.Id
            name = $_.Name
            file = $_.File
            offset = $_.Offset
            size = $_.Size
            crc32 = ("0x{0:X8}" -f $_.Crc32)
            format = "RGB565"
        }
    })
}
$Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutManifest -Encoding ASCII

Write-Output "Generated $OutBin"
Write-Output "Generated $OutManifest"
Write-Output "Version: $AssetVersion"
Write-Output "Mode: $AssetMode"
Write-Output "Size: $TotalSize bytes"
