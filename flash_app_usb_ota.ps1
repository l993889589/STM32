<#
.SYNOPSIS
Builds the STM32H563 application and installs it through the board USB OTA path.

.DESCRIPTION
This script is the non-SWD application update flow. By default it uses LDOT v2:
the device selects the inactive external firmware slot, accepts slot-relative
data offsets, verifies the complete image, and atomically publishes PENDING.

Use -LegacyV1 only for the one-time migration from firmware that does not yet
understand LDOT v2. Legacy mode writes the historical fixed download slot and
version-1 manifest copies.

It never writes the bootloader flash range. A bootloader change still requires a
separate protected programmer or factory procedure.
#>
param(
    [string]$Port = "",
    [uint32]$ImageVersion = 0,
    [string]$InputFile = "",
    [string]$PackageName = "usb_ota_app",
    [string]$OutputDir = "",
    [int]$BaudRate = 115200,
    [int]$InterFrameDelayMs = 2,
    [switch]$NoBuild,
    [switch]$NoReset,
    [switch]$LegacyV1
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$AppProject = Join-Path $Root "STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$AppBuildLog = Join-Path $Root "STM32H563_App\MDK-ARM\build_app_usb_ota.log"
$MakePackage = Join-Path $Root "make_ota_package.ps1"

$CmdBegin = 1
$CmdData = 2
$CmdManifest = 3
$CmdEnd = 4
$CmdReset = 5
$CmdFirmwareBegin = 32
$CmdFirmwareData = 33
$CmdFirmwareFinish = 34
$MaxPayload = 224
$ManifestA = [uint32]0x00000000
$ManifestB = [uint32]0x00001000

if($ImageVersion -eq 0) {
    $ImageVersion = [uint32](Get-Date -Format "yyyyMMddHH")
}

if([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $Root "ota_package"
}

function Invoke-AppBuild {
    <#
    .SYNOPSIS
    Runs the Keil application build and checks the generated build log.
    #>
    if(!(Test-Path -LiteralPath $Uv4)) {
        throw "UV4.exe not found: $Uv4"
    }
    if(!(Test-Path -LiteralPath $AppProject)) {
        throw "Application project not found: $AppProject"
    }

    Write-Host "Building application and waiting for Keil to exit..."
    $process = Start-Process -FilePath $Uv4 `
                             -ArgumentList @("-r", "`"$AppProject`"", "-j0", "-o", "`"$AppBuildLog`"") `
                             -WindowStyle Hidden `
                             -Wait `
                             -PassThru
    $exitCode = $process.ExitCode
    if(Test-Path -LiteralPath $AppBuildLog) {
        Get-Content -LiteralPath $AppBuildLog -Tail 30
    }
    $buildOk = (Test-Path -LiteralPath $AppBuildLog) -and
               (Select-String -LiteralPath $AppBuildLog -Pattern "0 Error\(s\)" -Quiet)
    if(($exitCode -ne 0) -and !$buildOk) {
        throw "Application build failed, exit code $exitCode"
    }
}

function Find-UsbCdcPort {
    <#
    .SYNOPSIS
    Returns the first USB CDC-style COM port exposed by the board.
    #>
    $ports = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match "\(COM\d+\)" -and $_.Name -match "USB|CDC|STMicro|Virtual|Serial" }
    if($ports.Count -eq 0) {
        $ports = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\(COM\d+\)" }
    }
    if($ports.Count -eq 0) {
        throw "No COM port found"
    }
    if($ports[0].Name -match "(COM\d+)") {
        return $Matches[1]
    }
    throw "No COM port found"
}

function Set-U16Le([byte[]]$Data, [int]$Offset, [uint16]$Value) {
    $Data[$Offset + 0] = [byte]($Value -band 0xFF)
    $Data[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
}

function Set-U32Le([byte[]]$Data, [int]$Offset, [uint32]$Value) {
    $Data[$Offset + 0] = [byte]($Value -band 0xFF)
    $Data[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Data[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Data[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

function Convert-HexU32([string]$Value) {
    <#
    .SYNOPSIS
    Parses either decimal text or 0x-prefixed manifest values into UInt32.
    #>
    if($Value -match "^0x") {
        return [uint32][Convert]::ToUInt32($Value.Substring(2), 16)
    }
    return [uint32]$Value
}

function Convert-HexBytes([string]$Value) {
    <#
    .SYNOPSIS
    Converts an even-length hexadecimal string into a byte array.
    #>
    if(($Value.Length % 2) -ne 0) {
        throw "Invalid hexadecimal byte string length: $($Value.Length)"
    }
    $bytes = [byte[]]::new($Value.Length / 2)
    for($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($Value.Substring($i * 2, 2), 16)
    }
    return ,$bytes
}

function Get-Crc16Modbus([byte[]]$Data, [int]$Length) {
    <#
    .SYNOPSIS
    Calculates the LDOT frame CRC used by the firmware USB OTA parser.
    #>
    [uint16]$crc = 0xFFFF
    for($i = 0; $i -lt $Length; $i++) {
        $crc = [uint16]($crc -bxor $Data[$i])
        for($bit = 0; $bit -lt 8; $bit++) {
            if(($crc -band 1) -ne 0) {
                $crc = [uint16](($crc -shr 1) -bxor 0xA001)
            } else {
                $crc = [uint16]($crc -shr 1)
            }
        }
    }
    return $crc
}

function New-LdotFrame([int]$Cmd, [uint16]$Seq, [uint32]$Address, [byte[]]$Payload) {
    <#
    .SYNOPSIS
    Builds one binary-safe LDOT frame for the application USB OTA parser.
    #>
    if($null -eq $Payload) {
        $Payload = [byte[]]::new(0)
    }
    if($Payload.Length -gt $MaxPayload) {
        throw "Payload too large: $($Payload.Length)"
    }

    $frame = [byte[]]::new(16 + $Payload.Length + 2)
    $frame[0] = [byte][char]'L'
    $frame[1] = [byte][char]'D'
    $frame[2] = [byte][char]'O'
    $frame[3] = [byte][char]'T'
    $frame[4] = [byte]$Cmd
    $frame[5] = 0
    Set-U16Le $frame 6 $Seq
    Set-U32Le $frame 8 $Address
    Set-U16Le $frame 12 ([uint16]$Payload.Length)
    $frame[14] = 0
    $frame[15] = 0
    if($Payload.Length -ne 0) {
        [System.Buffer]::BlockCopy($Payload, 0, $frame, 16, $Payload.Length)
    }
    $crc = Get-Crc16Modbus $frame (16 + $Payload.Length)
    Set-U16Le $frame (16 + $Payload.Length) $crc
    return ,$frame
}

function Read-OtaAck($Serial, [int]$Cmd, [int]$Seq) {
    <#
    .SYNOPSIS
    Waits for the matching textual OTA acknowledgement from the board.
    #>
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $lastLine = ""
    while([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Serial.ReadLine()
            $lastLine = $line.Trim()
            if($line -match "ota ack\s+(\d+)\s+(\d+)\s+(\d+)") {
                $ackCmd = [int]$Matches[1]
                $ackSeq = [int]$Matches[2]
                $status = [int]$Matches[3]
                if($ackCmd -eq $Cmd -and $ackSeq -eq $Seq) {
                    if($status -ne 0) {
                        throw "Device rejected cmd=$Cmd seq=$Seq status=$status"
                    }
                    return
                }
            }
        } catch [TimeoutException] {
        }
    }
    throw "Timeout waiting ack cmd=$Cmd seq=$Seq last='$lastLine'"
}

function Send-LdotFrame($Serial, [int]$Cmd, [ref]$Seq, [uint32]$Address, [byte[]]$Payload) {
    <#
    .SYNOPSIS
    Sends one LDOT frame and advances the sequence after a successful ACK.
    #>
    $frame = New-LdotFrame $Cmd ([uint16]$Seq.Value) $Address $Payload
    $Serial.Write($frame, 0, $frame.Length)
    Read-OtaAck $Serial $Cmd $Seq.Value
    if($InterFrameDelayMs -gt 0) {
        Start-Sleep -Milliseconds $InterFrameDelayMs
    }
    $Seq.Value = [uint16]($Seq.Value + 1)
}

if(!$NoBuild) {
    Invoke-AppBuild
}

$packageArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $MakePackage,
                 "-ImageVersion", $ImageVersion,
                 "-PackageName", $PackageName,
                 "-OutputDir", $OutputDir)
if(![string]::IsNullOrWhiteSpace($InputFile)) {
    $packageArgs += @("-InputFile", $InputFile)
}

Write-Host "Creating OTA package..."
& powershell @packageArgs
if($LASTEXITCODE -ne 0) {
    throw "make_ota_package.ps1 failed, exit code $LASTEXITCODE"
}

$manifestJsonPath = Join-Path $OutputDir "$PackageName.manifest.json"
if(!(Test-Path -LiteralPath $manifestJsonPath)) {
    throw "Manifest JSON not found: $manifestJsonPath"
}
$manifestJson = Get-Content -Raw -LiteralPath $manifestJsonPath | ConvertFrom-Json
$appBin = [string]$manifestJson.app_bin
$manifestBin = [string]$manifestJson.manifest_bin
$packageAddress = Convert-HexU32 ([string]$manifestJson.package_address)

if(!(Test-Path -LiteralPath $appBin)) {
    throw "App binary not found: $appBin"
}
if(!(Test-Path -LiteralPath $manifestBin)) {
    throw "Manifest binary not found: $manifestBin"
}

$appData = [System.IO.File]::ReadAllBytes($appBin)
$manifestData = [System.IO.File]::ReadAllBytes($manifestBin)
if($appData.Length -eq 0) {
    throw "App binary is empty"
}
if($manifestData.Length -gt $MaxPayload) {
    throw "Manifest frame is too large: $($manifestData.Length)"
}

$descriptorData = [byte[]]::new(124)
Set-U32Le $descriptorData 0  2
Set-U32Le $descriptorData 4  ([uint32]$manifestJson.image_version)
Set-U32Le $descriptorData 8  ([uint32]$appData.Length)
Set-U32Le $descriptorData 12 (Convert-HexU32 ([string]$manifestJson.image_crc32))
Set-U32Le $descriptorData 16 (Convert-HexU32 ([string]$manifestJson.image_flags))
Set-U32Le $descriptorData 20 (Convert-HexU32 ([string]$manifestJson.load_address))
Set-U32Le $descriptorData 24 (Convert-HexU32 ([string]$manifestJson.entry_address))
$imageSha = Convert-HexBytes ([string]$manifestJson.image_sha256)
if($imageSha.Length -ne 32) {
    throw "Expected 32-byte SHA-256, got $($imageSha.Length) bytes"
}
[System.Buffer]::BlockCopy($imageSha, 0, $descriptorData, 28, $imageSha.Length)
$signature = Convert-HexBytes ([string]$manifestJson.signature)
if($signature.Length -ne 64) {
    throw "Expected 64-byte ECDSA signature, got $($signature.Length) bytes"
}
[System.Buffer]::BlockCopy($signature, 0, $descriptorData, 60, $signature.Length)

if([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Find-UsbCdcPort
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 1000
$serial.WriteTimeout = 5000
$serial.NewLine = "`n"
$serial.DtrEnable = $true
$serial.RtsEnable = $true

Write-Host "Opening $Port for USB OTA..."
$serial.Open()
try {
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    $serial.DiscardOutBuffer()

    [uint16]$seqValue = 1
    $seq = [ref]$seqValue
    if($LegacyV1) {
        $beginPayload = [byte[]]::new(8)
        Set-U32Le $beginPayload 0 ([uint32]$appData.Length)
        Set-U32Le $beginPayload 4 0
        Send-LdotFrame $serial $CmdBegin $seq $packageAddress $beginPayload
    } else {
        Send-LdotFrame $serial $CmdFirmwareBegin $seq 0 $descriptorData
    }

    for($offset = 0; $offset -lt $appData.Length; $offset += $MaxPayload) {
        $len = [Math]::Min($MaxPayload, $appData.Length - $offset)
        $payload = [byte[]]::new($len)
        [System.Buffer]::BlockCopy($appData, $offset, $payload, 0, $len)
        if($LegacyV1) {
            Send-LdotFrame $serial $CmdData $seq ([uint32]($packageAddress + $offset)) $payload
        } else {
            Send-LdotFrame $serial $CmdFirmwareData $seq ([uint32]$offset) $payload
        }

        if((($offset + $len) -eq $appData.Length) -or (($seq.Value % 128) -eq 0)) {
            Write-Host ("Uploaded {0}/{1}" -f ($offset + $len), $appData.Length)
        }
    }

    if($LegacyV1) {
        Send-LdotFrame $serial $CmdEnd $seq 0 ([byte[]]::new(0))
        Send-LdotFrame $serial $CmdManifest $seq $ManifestA $manifestData
        Send-LdotFrame $serial $CmdManifest $seq $ManifestB $manifestData
    } else {
        Send-LdotFrame $serial $CmdFirmwareFinish $seq 0 ([byte[]]::new(0))
    }

    if(!$NoReset) {
        Send-LdotFrame $serial $CmdReset $seq 0 ([byte[]]::new(0))
    }

    $mode = if($LegacyV1) { "legacy-v1" } else { "firmware-ab-v2" }
    Write-Host "USB OTA upload complete. Mode=$mode ImageVersion=$ImageVersion Size=$($appData.Length)"
}
finally {
    try {
        if($serial.IsOpen) {
            $serial.Close()
        }
    } catch {
        Write-Host "Serial port disappeared during reset; treating upload as complete."
    }
}
