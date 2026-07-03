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
  [string]$Pack = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkspaceRoot = Split-Path -Parent $ProjectRoot
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$UvProject = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BuildLog = Join-Path $ProjectRoot "MDK-ARM\build_from_cmsis_dap_script.log"
$Hex = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"
$PackDir = Join-Path $WorkspaceRoot "tools\cmsis-packs"
$DefaultPack = Join-Path $PackDir "Keil.STM32H5xx_DFP.1.2.0.pack"
$PackUrl = "https://www.keil.com/pack/Keil.STM32H5xx_DFP.1.2.0.pack"
$LocalPython = Join-Path $WorkspaceRoot "tools\python-3.12.4-embed-amd64\python.exe"
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

  Write-Host "Building Keil project..."
  & $Uv4 -r $UvProject -j0 -o $BuildLog
  $buildExit = if($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }

  if(Test-Path -LiteralPath $BuildLog) {
    Get-Content -LiteralPath $BuildLog -Tail 40
  }

  $buildOk = $false
  if(Test-Path -LiteralPath $BuildLog) {
    $buildOk = (Select-String -LiteralPath $BuildLog -Pattern "0 Error\(s\)" -Quiet)
  }

  if(($buildExit -ne 0) -and !$buildOk) {
    throw "Keil build failed, exit code $buildExit"
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

$packPath = Get-PackPath
$flashArgs = @("flash", "--target", $Target, "--frequency", $Frequency, "--pack", $packPath, "--erase", "sector")

if($Probe -ne "") {
  $flashArgs += @("--uid", $Probe)
}

if($NoVerify) {
  $flashArgs += "--no-verify"
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

Write-Host "Done."

