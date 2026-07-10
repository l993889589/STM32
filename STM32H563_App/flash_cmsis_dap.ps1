param(
  [switch]$Build,
  [switch]$BuildOnly,
  [switch]$NoVerify,
  [switch]$List,
  [switch]$DownloadPack,
  [switch]$SetupPack,
  [string]$Target = "stm32h563rivx",
  [string]$Probe = "",
  [string]$Frequency = "1000000",
  [string]$Pack = "",
  [int]$WaitMqttSeconds = 0,
  [string]$MqttLog = "",
  [string]$ExpectedFwBuildId = ""
)

<#
  flash_cmsis_dap.ps1

  Purpose:
    Rebuild and program the STM32H563 application image at the bootloader app
    offset. This script is the daily flashing entry point for the app project.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\flash_cmsis_dap.ps1" -Build

  Constraints:
    The script flashes the Keil-generated HEX from the app project output
    directory. pyOCD verification is enabled by default; manual Flash readback
    should be reserved for suspected address, bootloader, or probe problems.
#>

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkspaceRoot = Split-Path -Parent $ProjectRoot
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$UvProject = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BuildLog = Join-Path $ProjectRoot "MDK-ARM\build_from_cmsis_dap_script.log"
$Hex = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"
$Axf = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.axf"
$AppEraseRange = "0x08020000-0x08200000"
$PackDir = Join-Path $WorkspaceRoot "tools\cmsis-packs"
$DefaultPack = Join-Path $PackDir "Keil.STM32H5xx_DFP.1.2.0.pack"
$PackUrl = "https://www.keil.com/pack/Keil.STM32H5xx_DFP.1.2.0.pack"
$LocalPython = Join-Path $WorkspaceRoot "tools\python-3.12.4-embed-amd64\python.exe"
$ScriptStartUtc = [DateTime]::UtcNow
$script:MqttVerifyAfterUtc = $ScriptStartUtc
$script:PyOcdExitCode = 0

function Invoke-PyOcd {
  param([string[]]$PyOcdArgs)

  if(Test-Path -LiteralPath $LocalPython) {
    & $LocalPython -m pyocd @PyOcdArgs
    $script:PyOcdExitCode = $LASTEXITCODE
    return
  }

  $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
  if($pyLauncher) {
    & py -m pyocd @PyOcdArgs
    $script:PyOcdExitCode = $LASTEXITCODE
    return
  }

  $python = Get-Command python -ErrorAction SilentlyContinue
  if($python) {
    & python -m pyocd @PyOcdArgs
    $script:PyOcdExitCode = $LASTEXITCODE
    return
  }

  throw "Python launcher not found. Run first: powershell -NoProfile -ExecutionPolicy Bypass -File `"$WorkspaceRoot\tools\setup_pyocd.ps1`""
}

function Test-PyOcd {
  Invoke-PyOcd @("--version") | Out-Null
  if($script:PyOcdExitCode -ne 0) {
    throw "pyOCD is not available. Run first: powershell -NoProfile -ExecutionPolicy Bypass -File `"$WorkspaceRoot\tools\setup_pyocd.ps1`""
  }
}

function Get-PackPath {
  if($Pack -ne "") {
    if(!(Test-Path -LiteralPath $Pack)) {
      throw "CMSIS pack not found: $Pack"
    }
    return (Resolve-Path -LiteralPath $Pack).Path
  }

  if(Test-Path -LiteralPath $DefaultPack) {
    return (Resolve-Path -LiteralPath $DefaultPack).Path
  }

  $downloadCache = Join-Path $env:LOCALAPPDATA "Arm\Packs\.Download\Keil.STM32H5xx_DFP.1.2.0.pack"
  if(Test-Path -LiteralPath $downloadCache) {
    return (Resolve-Path -LiteralPath $downloadCache).Path
  }

  if($DownloadPack) {
    New-Item -ItemType Directory -Path $PackDir -Force | Out-Null
    Write-Host "Downloading CMSIS pack: $PackUrl"
    Invoke-WebRequest -Uri $PackUrl -OutFile $DefaultPack
    return (Resolve-Path -LiteralPath $DefaultPack).Path
  }

  throw @"
CMSIS pack is required for pyOCD flash programming.
Expected: $DefaultPack

Run once:
  powershell -NoProfile -ExecutionPolicy Bypass -File "$PSCommandPath" -DownloadPack -List

Or download manually:
  $PackUrl

Then flash with:
  powershell -NoProfile -ExecutionPolicy Bypass -File "$PSCommandPath" -Build
"@
}

function Resolve-MqttLogPath {
  if($MqttLog -ne "") {
    if(!(Test-Path -LiteralPath $MqttLog)) {
      throw "MQTT log not found: $MqttLog"
    }
    return (Resolve-Path -LiteralPath $MqttLog).Path
  }

  $embeddedRoot = Split-Path -Parent $WorkspaceRoot

  # Prefer the process that actually owns the broker listener. This avoids
  # matching stale backup copies of desktop-debug-assistant elsewhere under
  # D:\Embedded and turning a successful flash into a false wait timeout.
  $listener = Get-NetTCPConnection -LocalPort 1883 -State Listen -ErrorAction SilentlyContinue |
              Select-Object -First 1
  if($null -ne $listener) {
    $owner = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
    if($null -ne $owner -and $owner.Path) {
      $activeLog = Join-Path (Split-Path -Parent $owner.Path) "logs\latest.log"
      if(Test-Path -LiteralPath $activeLog) {
        return (Resolve-Path -LiteralPath $activeLog).Path
      }
    }
  }

  $assistantDirs = Get-ChildItem -Path $embeddedRoot -Directory -Recurse -Filter "desktop-debug-assistant" -ErrorAction SilentlyContinue
  $logCandidates = foreach($assistantDir in $assistantDirs) {
    foreach($relative in @("release\win-unpacked\logs\latest.log", "logs\latest.log")) {
      $candidate = Join-Path $assistantDir.FullName $relative
      if(Test-Path -LiteralPath $candidate) {
        Get-Item -LiteralPath $candidate
      }
    }
  }
  $newestLog = $logCandidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if($null -ne $newestLog) {
    return $newestLog.FullName
  }

  throw "MQTT latest.log not found under $embeddedRoot. Pass -MqttLog explicitly."
}

function Wait-MqttOnline {
  param(
    [int]$TimeoutSeconds,
    [string]$ExpectedBuild
  )

  if($TimeoutSeconds -le 0) {
    return
  }

  $logPath = Resolve-MqttLogPath
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $lastBuild = ""

  Write-Host "Waiting for W800 MQTT status:"
  Write-Host "  Log      : $logPath"
  Write-Host "  Timeout  : $TimeoutSeconds seconds"
  if($ExpectedBuild -ne "") {
    Write-Host "  Expected : $ExpectedBuild"
  }

  while((Get-Date) -lt $deadline) {
    if(Test-Path -LiteralPath $logPath) {
      $lines = Get-Content -LiteralPath $logPath -Tail 120 -ErrorAction SilentlyContinue
      foreach($line in $lines) {
        if($line -notmatch "leduo/w800/status" -or $line -notmatch "fwBuildId") {
          continue
        }

        $lineUtc = $null
        if($line -match "^\[(?<ts>[^\]]+)\]") {
          try {
            $lineUtc = ([DateTime]::Parse($Matches.ts)).ToUniversalTime()
          } catch {
            $lineUtc = $null
          }
        }
        if($null -ne $lineUtc -and $lineUtc -lt $script:MqttVerifyAfterUtc) {
          continue
        }

        if($line -notmatch "(\{.*\})\s*$") {
          continue
        }
        try {
          $status = $Matches[1] | ConvertFrom-Json
        } catch {
          continue
        }

        $lastBuild = [string]$status.fwBuildId
        if($ExpectedBuild -eq "" -or $lastBuild -eq $ExpectedBuild) {
          Write-Host "W800 MQTT status matched:"
          Write-Host "  fwBuildId: $lastBuild"
          Write-Host "  mode     : $($status.mode)"
          Write-Host "  asset    : version=$($status.asset.version) error=$($status.asset.error)"
          return
        }
      }
    }

    Start-Sleep -Seconds 2
  }

  if($ExpectedBuild -ne "") {
    throw "Timed out waiting for fwBuildId '$ExpectedBuild'. Last seen after flash: '$lastBuild'."
  }
  throw "Timed out waiting for W800 MQTT status after flash."
}

function Get-AxfFirmwareBuildId {
  param([string]$Path)

  if(!(Test-Path -LiteralPath $Path)) {
    return ""
  }

  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
  $matches = [regex]::Matches(
    $ascii,
    "[A-Z][a-z]{2}\s{1,2}\d{1,2}\s\d{4}\s\d{2}:\d{2}:\d{2}"
  )
  if($matches.Count -eq 0) {
    return ""
  }

  return $matches[$matches.Count - 1].Value
}

Test-PyOcd

if($List) {
  Write-Host "Detected CMSIS-DAP probes:"
  Invoke-PyOcd @("list", "--probes")
  exit $script:PyOcdExitCode
}

if($SetupPack) {
  $packPath = Get-PackPath
  Write-Host "CMSIS pack ready: $packPath"
  Write-Host "Checking STM32H563 targets in pack..."
  Invoke-PyOcd @("list", "--targets", "--source", "pack", "--pack", $packPath)
  exit $script:PyOcdExitCode
}

if($Build) {
  if(!(Test-Path -LiteralPath $Uv4)) {
    throw "UV4.exe not found: $Uv4"
  }

  $buildStartUtc = [DateTime]::UtcNow
  Remove-Item -LiteralPath $BuildLog -Force -ErrorAction SilentlyContinue

  Write-Host "Building Keil project and waiting for UV4 to exit..."
  $buildProcess = Start-Process -FilePath $Uv4 `
                                -ArgumentList @("-r", "`"$UvProject`"", "-j0", "-o", "`"$BuildLog`"") `
                                -WindowStyle Hidden `
                                -Wait `
                                -PassThru
  $buildExit = $buildProcess.ExitCode

  if(Test-Path -LiteralPath $BuildLog) {
    Get-Content -LiteralPath $BuildLog -Tail 40
  }

  $buildOk = $false
  if(Test-Path -LiteralPath $BuildLog) {
    $buildOk = (Select-String -LiteralPath $BuildLog -Pattern "0 Error\(s\)" -Quiet)
  }

  if(!$buildOk) {
    throw "Keil build failed, exit code $buildExit"
  }

  foreach($artifact in @($BuildLog, $Hex, $Axf)) {
    if(!(Test-Path -LiteralPath $artifact)) {
      throw "Keil build did not produce required artifact: $artifact"
    }
    $item = Get-Item -LiteralPath $artifact
    if($item.LastWriteTimeUtc -lt $buildStartUtc.AddSeconds(-2)) {
      throw "Keil returned before refreshing artifact: $artifact ($($item.LastWriteTime))"
    }
  }

  if($BuildOnly) {
    Write-Host "Build-only mode: skip programming."
    return
  }
}

if($BuildOnly) {
  throw "-BuildOnly requires -Build."
}

if(!(Test-Path -LiteralPath $Hex)) {
  throw "HEX file not found: $Hex. Run with -Build first."
}

$hexItem = Get-Item -LiteralPath $Hex
Write-Host "Flash artifact:"
Write-Host "  HEX : $($hexItem.FullName)"
Write-Host "  Time: $($hexItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Host "  Size: $($hexItem.Length) bytes"
if(Test-Path -LiteralPath $Axf) {
  $axfItem = Get-Item -LiteralPath $Axf
  Write-Host "  AXF : $($axfItem.FullName)"
  Write-Host "  Time: $($axfItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"

  if($Build -and $WaitMqttSeconds -gt 0 -and $ExpectedFwBuildId -eq "") {
    $ExpectedFwBuildId = Get-AxfFirmwareBuildId -Path $Axf
    if($ExpectedFwBuildId -eq "") {
      throw "Unable to extract fwBuildId from freshly built AXF: $Axf"
    }
    Write-Host "  Build ID: $ExpectedFwBuildId"
  }
}

$packPath = Get-PackPath

Write-Host "Erasing application flash range before programming:"
Write-Host "  Range: $AppEraseRange"
Write-Host "  Note : bootloader area 0x08000000-0x0801FFFF is not touched."
$eraseArgs = @("erase", "--target", $Target, "--frequency", $Frequency, "--pack", $packPath, "--sector", $AppEraseRange)
if($Probe -ne "") {
  $eraseArgs += @("--uid", $Probe)
}
Invoke-PyOcd $eraseArgs
if($script:PyOcdExitCode -ne 0) {
  throw "pyOCD app-range erase failed, exit code $script:PyOcdExitCode"
}

$flashArgs = @("flash", "--target", $Target, "--frequency", $Frequency, "--pack", $packPath, "--erase", "sector")

if($Probe -ne "") {
  $flashArgs += @("--uid", $Probe)
}

if($NoVerify) {
  Write-Warning "Current pyOCD build does not accept --no-verify on this setup; continuing with normal verify."
}

$flashArgs += $Hex

Write-Host "Programming HEX with CMSIS-DAP/pyOCD..."
Write-Host "Target: $Target"
Write-Host "Pack  : $packPath"
Invoke-PyOcd $flashArgs
if($script:PyOcdExitCode -ne 0) {
  throw "pyOCD flash failed, exit code $script:PyOcdExitCode"
}

Write-Host "Resetting target..."
$resetArgs = @("reset", "--target", $Target, "--frequency", $Frequency, "--pack", $packPath)
if($Probe -ne "") {
  $resetArgs += @("--uid", $Probe)
}
Invoke-PyOcd $resetArgs
if($script:PyOcdExitCode -ne 0) {
  throw "pyOCD reset failed, exit code $script:PyOcdExitCode"
}
$script:MqttVerifyAfterUtc = [DateTime]::UtcNow

Wait-MqttOnline -TimeoutSeconds $WaitMqttSeconds -ExpectedBuild $ExpectedFwBuildId

Write-Host "Done."

