Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$PicDir = Join-Path $Root "assets\ui\source"
$UiDir = Join-Path $Root "STM32H563_App\user\ui\lvgl"
$OutC = Join-Path $UiDir "ref_page_img.c"
$OutH = Join-Path $UiDir "ref_page_img.h"

$PageList = @(
    @{ Id = "brand";   File = "beb0ed1df7c3de72abb4b0fd4229415a.png"; Comment = "brand page"; Format = "I8" },
    @{ Id = "monitor"; File = "70afbaff6c736f0d3a66b35c82c2bc38.png"; Comment = "CPU/GPU monitor page"; Format = "RGB565" },
    @{ Id = "sensor";  File = "808cb614ff3d111885ed7371d5d79891.png"; Comment = "sensor and disk page"; Format = "I8" },
    @{ Id = "event";   File = "5c3e140423c1eddbbb1ce1f46d3a637e.png"; Comment = "event list page"; Format = "I8" },
    @{ Id = "comm";    File = "10ee8bfe01457ec5cec32803e1811569.png"; Comment = "communication page"; Format = "RGB565"; Crop = @(68, 93, 890, 593) }
)

foreach($Page in $PageList) {
    $FullPath = Join-Path $PicDir $Page.File
    if(!(Test-Path -LiteralPath $FullPath)) {
        throw "Missing reference image: $FullPath"
    }
    $Page["FullPath"] = $FullPath
}

function Write-ByteArray([System.Text.StringBuilder]$Sb, [byte[]]$Data) {
    for($I = 0; $I -lt $Data.Length; $I++) {
        if(($I % 16) -eq 0) {
            [void]$Sb.Append("    ")
        }
        [void]$Sb.Append(("0x{0:X2}" -f $Data[$I]))
        if($I -lt ($Data.Length - 1)) {
            if((($I + 1) % 16) -eq 0) {
                [void]$Sb.Append(",")
            }
            else {
                [void]$Sb.Append(", ")
            }
        }
        if((($I + 1) % 16) -eq 0 -or $I -eq ($Data.Length - 1)) {
            [void]$Sb.AppendLine()
        }
    }
}

function Get-HistCodeR([int]$Code) { return (($Code -shr 8) -band 0x0F) }
function Get-HistCodeG([int]$Code) { return (($Code -shr 4) -band 0x0F) }
function Get-HistCodeB([int]$Code) { return ($Code -band 0x0F) }

function Get-BoxInfo([int[]]$Codes, [int[]]$Counts) {
    $MinR = 15; $MaxR = 0; $MinG = 15; $MaxG = 0; $MinB = 15; $MaxB = 0
    $Total = 0
    foreach($Code in $Codes) {
        $R = Get-HistCodeR $Code
        $G = Get-HistCodeG $Code
        $B = Get-HistCodeB $Code
        if($R -lt $MinR) { $MinR = $R }
        if($R -gt $MaxR) { $MaxR = $R }
        if($G -lt $MinG) { $MinG = $G }
        if($G -gt $MaxG) { $MaxG = $G }
        if($B -lt $MinB) { $MinB = $B }
        if($B -gt $MaxB) { $MaxB = $B }
        $Total += $Counts[$Code]
    }

    $RangeR = $MaxR - $MinR
    $RangeG = $MaxG - $MinG
    $RangeB = $MaxB - $MinB
    $Channel = 0
    $Range = $RangeR
    if($RangeG -gt $Range) { $Range = $RangeG; $Channel = 1 }
    if($RangeB -gt $Range) { $Range = $RangeB; $Channel = 2 }

    return [PSCustomObject]@{
        Codes = $Codes
        Count = $Total
        Channel = $Channel
        Score = ($Range * [Math]::Max(1, $Total))
        CanSplit = (($Codes.Length -gt 1) -and ($Range -gt 0))
    }
}

function Split-ColorBox($Box, [int[]]$Counts) {
    if($Box.Channel -eq 0) {
        $Sorted = @($Box.Codes | Sort-Object { Get-HistCodeR $_ })
    }
    elseif($Box.Channel -eq 1) {
        $Sorted = @($Box.Codes | Sort-Object { Get-HistCodeG $_ })
    }
    else {
        $Sorted = @($Box.Codes | Sort-Object { Get-HistCodeB $_ })
    }

    $Half = [Math]::Max(1, [int]($Box.Count / 2))
    $Acc = 0
    $Split = 1
    for($I = 0; $I -lt ($Sorted.Length - 1); $I++) {
        $Acc += $Counts[$Sorted[$I]]
        if($Acc -ge $Half) {
            $Split = $I + 1
            break
        }
    }

    $A = @($Sorted[0..($Split - 1)])
    $B = @($Sorted[$Split..($Sorted.Length - 1)])
    return @(Get-BoxInfo $A $Counts), @(Get-BoxInfo $B $Counts)
}

function New-AdaptivePalette([System.Drawing.Bitmap]$Bmp) {
    $Counts = New-Object int[] 4096
    $SumR = New-Object Int64[] 4096
    $SumG = New-Object Int64[] 4096
    $SumB = New-Object Int64[] 4096

    for($Y = 0; $Y -lt $Bmp.Height; $Y++) {
        for($X = 0; $X -lt $Bmp.Width; $X++) {
            $C = $Bmp.GetPixel($X, $Y)
            $Code = (($C.R -shr 4) -shl 8) -bor (($C.G -shr 4) -shl 4) -bor ($C.B -shr 4)
            $Counts[$Code]++
            $SumR[$Code] += $C.R
            $SumG[$Code] += $C.G
            $SumB[$Code] += $C.B
        }
    }

    $CodesList = [System.Collections.Generic.List[int]]::new()
    for($Code = 0; $Code -lt 4096; $Code++) {
        if($Counts[$Code] -gt 0) {
            $CodesList.Add($Code)
        }
    }

    $Boxes = [System.Collections.ArrayList]::new()
    [void]$Boxes.Add((Get-BoxInfo ([int[]]$CodesList.ToArray()) $Counts))
    while($Boxes.Count -lt 256) {
        $BestIndex = -1
        $BestScore = -1
        for($I = 0; $I -lt $Boxes.Count; $I++) {
            if($Boxes[$I].CanSplit -and $Boxes[$I].Score -gt $BestScore) {
                $BestScore = $Boxes[$I].Score
                $BestIndex = $I
            }
        }

        if($BestIndex -lt 0) {
            break
        }

        $SplitBoxes = Split-ColorBox $Boxes[$BestIndex] $Counts
        $Boxes.RemoveAt($BestIndex)
        [void]$Boxes.Add($SplitBoxes[0])
        [void]$Boxes.Add($SplitBoxes[1])
    }

    $Palette = New-Object byte[] (256 * 4)
    $PaletteR = New-Object byte[] 256
    $PaletteG = New-Object byte[] 256
    $PaletteB = New-Object byte[] 256

    for($I = 0; $I -lt $Boxes.Count; $I++) {
        $Total = 0
        [Int64]$R = 0
        [Int64]$G = 0
        [Int64]$B = 0
        foreach($Code in $Boxes[$I].Codes) {
            $Total += $Counts[$Code]
            $R += $SumR[$Code]
            $G += $SumG[$Code]
            $B += $SumB[$Code]
        }
        if($Total -le 0) { $Total = 1 }

        $PaletteR[$I] = [byte][Math]::Min(255, [int][Math]::Round($R / [double]$Total))
        $PaletteG[$I] = [byte][Math]::Min(255, [int][Math]::Round($G / [double]$Total))
        $PaletteB[$I] = [byte][Math]::Min(255, [int][Math]::Round($B / [double]$Total))
    }

    for($I = $Boxes.Count; $I -lt 256; $I++) {
        $Copy = [Math]::Max(0, $Boxes.Count - 1)
        $PaletteR[$I] = $PaletteR[$Copy]
        $PaletteG[$I] = $PaletteG[$Copy]
        $PaletteB[$I] = $PaletteB[$Copy]
    }

    for($I = 0; $I -lt 256; $I++) {
        $Base = $I * 4
        $Palette[$Base + 0] = $PaletteB[$I]
        $Palette[$Base + 1] = $PaletteG[$I]
        $Palette[$Base + 2] = $PaletteR[$I]
        $Palette[$Base + 3] = 0xFF
    }

    $IndexMap = New-Object byte[] 4096
    for($Code = 0; $Code -lt 4096; $Code++) {
        $R = (Get-HistCodeR $Code) * 17
        $G = (Get-HistCodeG $Code) * 17
        $B = (Get-HistCodeB $Code) * 17
        $Best = 0
        $BestDist = [int]::MaxValue
        for($I = 0; $I -lt 256; $I++) {
            $Dr = $R - $PaletteR[$I]
            $Dg = $G - $PaletteG[$I]
            $Db = $B - $PaletteB[$I]
            $Dist = ($Dr * $Dr) + ($Dg * $Dg) + ($Db * $Db)
            if($Dist -lt $BestDist) {
                $BestDist = $Dist
                $Best = $I
            }
        }
        $IndexMap[$Code] = [byte]$Best
    }

    return [PSCustomObject]@{
        Palette = $Palette
        IndexMap = $IndexMap
    }
}

function Convert-ToI8Map([System.Drawing.Bitmap]$Bmp, [byte[]]$IndexMap) {
    $Width = $Bmp.Width
    $Height = $Bmp.Height
    $Pixels = New-Object byte[] ($Width * $Height)

    for($Y = 0; $Y -lt $Height; $Y++) {
        for($X = 0; $X -lt $Width; $X++) {
            $C = $Bmp.GetPixel($X, $Y)
            $Code = (($C.R -shr 4) -shl 8) -bor (($C.G -shr 4) -shl 4) -bor ($C.B -shr 4)
            $Pixels[($Y * $Width) + $X] = $IndexMap[$Code]
        }
    }

    return ,$Pixels
}

function Convert-ToRgb565Map([System.Drawing.Bitmap]$Bmp) {
    $Width = $Bmp.Width
    $Height = $Bmp.Height
    $Pixels = New-Object byte[] ($Width * $Height * 2)
    $Out = 0

    for($Y = 0; $Y -lt $Height; $Y++) {
        for($X = 0; $X -lt $Width; $X++) {
            $C = $Bmp.GetPixel($X, $Y)
            $V = (($C.R -band 0xF8) -shl 8) -bor (($C.G -band 0xFC) -shl 3) -bor ($C.B -shr 3)
            $Pixels[$Out] = [byte]($V -band 0xFF)
            $Pixels[$Out + 1] = [byte](($V -shr 8) -band 0xFF)
            $Out += 2
        }
    }

    return ,$Pixels
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
        Fill-Rect $Bmp 60 125 78 34 "#0E2246"
        Fill-Rect $Bmp 155 124 66 35 "#10243A"
        Fill-Rect $Bmp 292 125 82 34 "#0D2D3A"
        Fill-Rect $Bmp 386 124 66 35 "#10263C"
        Fill-Rect $Bmp 80 262 92 34 "#10233D"
        Fill-Rect $Bmp 230 262 88 34 "#102A35"
        Fill-Rect $Bmp 389 262 58 34 "#221E1A"
    }
    elseif($PageId -eq "sensor") {
        Fill-Rect $Bmp 68 119 74 34 "#112A46"
        Fill-Rect $Bmp 222 119 74 34 "#112A46"
        Fill-Rect $Bmp 369 119 78 34 "#112A46"
        Fill-Rect $Bmp 393 18 72 22 "#0D1D33"
    }
    elseif($PageId -eq "comm") {
        Fill-Rect $Bmp 356 30 83 26 "#111B27"
        Fill-Rect $Bmp 38 103 86 23 "#20734D"
        Fill-Rect $Bmp 180 103 55 23 "#7A4A20"
        Fill-Rect $Bmp 323 103 67 23 "#813B62"
        Fill-Rect $Bmp 44 162 133 61 "#122D38"
        Fill-Rect $Bmp 352 165 97 41 "#162C3A"
        Fill-Rect $Bmp 355 230 67 39 "#162C3A"
    }
}

$Header = [System.Collections.Generic.List[string]]::new()
$Header.Add("#ifndef REF_PAGE_IMG_H")
$Header.Add("#define REF_PAGE_IMG_H")
$Header.Add("")
$Header.Add("#include ""lvgl.h""")
$Header.Add("")
$Header.Add("#define REF_PAGE_WIDTH  480U")
$Header.Add("#define REF_PAGE_HEIGHT 320U")
$Header.Add("#define REF_PAGE_COUNT  5U")
$Header.Add("")
$Header.Add("#define REF_PAGE_BRAND_INDEX   0U")
$Header.Add("#define REF_PAGE_MONITOR_INDEX 1U")
$Header.Add("#define REF_PAGE_SENSOR_INDEX  2U")
$Header.Add("#define REF_PAGE_EVENT_INDEX   3U")
$Header.Add("#define REF_PAGE_COMM_INDEX    4U")
$Header.Add("")
foreach($Page in $PageList) {
    $Header.Add("extern const lv_image_dsc_t ref_page_$($Page.Id);")
}
$Header.Add("extern const lv_image_dsc_t *const ref_pages[REF_PAGE_COUNT];")
$Header.Add("")
$Header.Add("#endif /* REF_PAGE_IMG_H */")
[System.IO.File]::WriteAllLines($OutH, $Header, [System.Text.Encoding]::ASCII)

$Sb = [System.Text.StringBuilder]::new()
[void]$Sb.AppendLine('#include "ref_page_img.h"')
[void]$Sb.AppendLine("")
[void]$Sb.AppendLine("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
[void]$Sb.AppendLine("#define LV_ATTRIBUTE_MEM_ALIGN")
[void]$Sb.AppendLine("#endif")
[void]$Sb.AppendLine("#ifndef LV_ATTRIBUTE_IMAGE_REF_PAGE")
[void]$Sb.AppendLine("#define LV_ATTRIBUTE_IMAGE_REF_PAGE")
[void]$Sb.AppendLine("#endif")
[void]$Sb.AppendLine("")

foreach($Page in $PageList) {
    $Src = [System.Drawing.Image]::FromFile($Page.FullPath)
    $Bmp = New-Object System.Drawing.Bitmap 480, 320, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $G = [System.Drawing.Graphics]::FromImage($Bmp)
    $G.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $G.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $G.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    if($Page.ContainsKey("Crop")) {
        $Crop = $Page.Crop
        $Dst = New-Object System.Drawing.Rectangle 0, 0, 480, 320
        $G.DrawImage($Src, $Dst, $Crop[0], $Crop[1], $Crop[2], $Crop[3],
                     [System.Drawing.GraphicsUnit]::Pixel)
    }
    else {
        $G.DrawImage($Src, 0, 0, 480, 320)
    }
    $G.Dispose()
    $Src.Dispose()

    Clear-StaticValues $Bmp $Page.Id

    if($Page.Format -eq "RGB565") {
        $ImageMap = Convert-ToRgb565Map $Bmp
        $ColorFormat = "LV_COLOR_FORMAT_RGB565"
        $Stride = 480 * 2
    }
    else {
        $Quant = New-AdaptivePalette $Bmp
        $PixelMap = Convert-ToI8Map $Bmp $Quant.IndexMap
        $ImageMap = New-Object byte[] ($Quant.Palette.Length + $PixelMap.Length)
        [System.Buffer]::BlockCopy($Quant.Palette, 0, $ImageMap, 0, $Quant.Palette.Length)
        [System.Buffer]::BlockCopy($PixelMap, 0, $ImageMap, $Quant.Palette.Length, $PixelMap.Length)
        $ColorFormat = "LV_COLOR_FORMAT_I8"
        $Stride = 480
    }
    $Bmp.Dispose()

    $ArrayName = "ref_page_$($Page.Id)_map"
    [void]$Sb.AppendLine("/* Source: assets/ui/source/$($Page.File) ($($Page.Comment), $($Page.Format)) */")
    [void]$Sb.AppendLine("const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_REF_PAGE uint8_t $ArrayName[] = {")
    Write-ByteArray $Sb $ImageMap
    [void]$Sb.AppendLine("};")
    [void]$Sb.AppendLine("")
    [void]$Sb.AppendLine("const lv_image_dsc_t ref_page_$($Page.Id) = {")
    [void]$Sb.AppendLine("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
    [void]$Sb.AppendLine("    .header.cf = $ColorFormat,")
    [void]$Sb.AppendLine("    .header.w = 480,")
    [void]$Sb.AppendLine("    .header.h = 320,")
    [void]$Sb.AppendLine("    .header.stride = $Stride,")
    [void]$Sb.AppendLine("    .data_size = sizeof($ArrayName),")
    [void]$Sb.AppendLine("    .data = $ArrayName,")
    [void]$Sb.AppendLine("};")
    [void]$Sb.AppendLine("")
}

[void]$Sb.AppendLine("const lv_image_dsc_t *const ref_pages[REF_PAGE_COUNT] = {")
for($I = 0; $I -lt $PageList.Count; $I++) {
    [void]$Sb.Append("    &ref_page_$($PageList[$I].Id)")
    if($I -lt ($PageList.Count - 1)) {
        [void]$Sb.Append(",")
    }
    [void]$Sb.AppendLine("")
}
[void]$Sb.AppendLine("};")

[System.IO.File]::WriteAllText($OutC, $Sb.ToString(), [System.Text.Encoding]::ASCII)
Write-Output "Generated $OutC"
Write-Output "Generated $OutH"
Write-Output "Image count: $($PageList.Count)"
foreach($Page in $PageList) {
    Write-Output "  $($Page.Id): $($Page.File) [$($Page.Format)]"
}
