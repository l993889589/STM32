param(
  [switch]$NoBuild,
  [switch]$NoVerify,
  [switch]$AppOnly,
  [switch]$BootOnly,
  [ValidateSet("auto", "stlink", "daplink")]
  [string]$ProbeType = "auto",
  [string]$ConnectMode = "HotPlug",
  [string]$Target = "stm32h563rivx",
  [string]$Probe = "",
  [string]$Frequency = "1000000",
  [string]$Pack = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$CubeProgrammer = "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

$BootProject = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BootLog = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\build_bootloader_flash_all.log"
$BootHex = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Bootloader\STM32H563_Bootloader.hex"

$AppRootCandidates = @(
  (Join-Path $Root "STM32H563_App"),
  (Join-Path $Root "STM32H563_Threadx_usbx_cdc_acm")
)
$AppRoot = $AppRootCandidates | Where-Object {
  Test-Path -LiteralPath (Join-Path $_ "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx")
} | Select-Object -First 1
if (!$AppRoot) {
  throw "Application project not found. Checked: $($AppRootCandidates -join ', ')"
}

$AppProject = Join-Path $AppRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$AppLog = Join-Path $AppRoot "MDK-ARM\build_app_flash_all.log"
$AppHex = Join-Path $AppRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"

$PackDir = Join-Path $Root "tools\cmsis-packs"
$DefaultPack = Join-Path $PackDir "Keil.STM32H5xx_DFP.1.2.0.pack"
$PackUrl = "https://www.keil.com/pack/Keil.STM32H5xx_DFP.1.2.0.pack"
$LocalPython = Join-Path $Root "tools\python-3.12.4-embed-amd64\python.exe"
$script:PyOcdExitCode = 0

if ($AppOnly -and $BootOnly) {
  throw "-AppOnly and -BootOnly cannot be used together."
}

function Invoke-KeilBuild {
  param(
    [string]$Project,
    [string]$Log,
    [string]$Name
  )

  Write-Host "Building $Name..."
  & $Uv4 -r $Project -j0 -o $Log
  $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }

  if (Test-Path -LiteralPath $Log) {
    Get-Content -LiteralPath $Log -Tail 30
  }

  $buildOk = $false
  if (Test-Path -LiteralPath $Log) {
    $buildOk = (Select-String -LiteralPath $Log -Pattern "0 Error\(s\)" -Quiet)
  }

  if (($exitCode -ne 0) -and !$buildOk) {
    throw "$Name build failed, exit code $exitCode"
  }

  if (($exitCode -ne 0) -and $buildOk) {
    Write-Host "$Name build returned exit code $exitCode, but log reports 0 errors. Continuing..."
  }
}

function Get-PythonCommand {
  if (Test-Path -LiteralPath $LocalPython) {
    return @{ File = $LocalPython; Prefix = @("-m", "pyocd") }
  }

  $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
  if ($pyLauncher) {
    return @{ File = "py"; Prefix = @("-m", "pyocd") }
  }

  $python = Get-Command python -ErrorAction SilentlyContinue
  if ($python) {
    return @{ File = "python"; Prefix = @("-m", "pyocd") }
  }

  throw "Python launcher not found. Run first: powershell -NoProfile -ExecutionPolicy Bypass -File `"$Root\tools\setup_pyocd.ps1`""
}

function Invoke-PyOcd {
  param([string[]]$PyOcdArgs)

  $cmd = Get-PythonCommand
  & $cmd.File @($cmd.Prefix + $PyOcdArgs)
  $script:PyOcdExitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
}

function Invoke-PyOcdText {
  param([string[]]$PyOcdArgs)

  $cmd = Get-PythonCommand
  $output = & $cmd.File @($cmd.Prefix + $PyOcdArgs) 2>&1
  $script:PyOcdExitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
  return ($output | Out-String)
}

function Test-PyOcd {
  Invoke-PyOcd @("--version")
  if ($script:PyOcdExitCode -ne 0) {
    throw "pyOCD is not available. Run first: powershell -NoProfile -ExecutionPolicy Bypass -File `"$Root\tools\setup_pyocd.ps1`""
  }
}

function Test-CmsisDapProbe {
  try {
    $text = Invoke-PyOcdText @("list", "--probes")
    Write-Host $text
    return (($script:PyOcdExitCode -eq 0) -and ($text -match "CMSIS-DAP|DAPLink|Unique ID|H7-TOOL"))
  } catch {
    Write-Host "CMSIS-DAP detection failed: $($_.Exception.Message)"
    return $false
  }
}

function Get-PackPath {
  if ($Pack -ne "") {
    if (!(Test-Path -LiteralPath $Pack)) {
      throw "CMSIS pack not found: $Pack"
    }
    return (Resolve-Path -LiteralPath $Pack).Path
  }

  if (Test-Path -LiteralPath $DefaultPack) {
    return (Resolve-Path -LiteralPath $DefaultPack).Path
  }

  $downloadCache = Join-Path $env:LOCALAPPDATA "Arm\Packs\.Download\Keil.STM32H5xx_DFP.1.2.0.pack"
  if (Test-Path -LiteralPath $downloadCache) {
    return (Resolve-Path -LiteralPath $downloadCache).Path
  }

  throw @"
CMSIS pack is required for pyOCD flash programming.
Expected: $DefaultPack

Run once:
  powershell -NoProfile -ExecutionPolicy Bypass -File "$Root\STM32H563_App\flash_cmsis_dap.ps1" -DownloadPack -List

Or download manually:
  $PackUrl
"@
}

function Write-HexByStlink {
  param(
    [string]$Hex,
    [string]$Name
  )

  if (!(Test-Path -LiteralPath $Hex)) {
    throw "$Name HEX file not found: $Hex"
  }

  $connectArgs = @("-c", "port=SWD", "mode=$ConnectMode", "reset=SWrst")
  $writeArgs = @("-w", $Hex)

  if (!$NoVerify) {
    $writeArgs += "-v"
  }

  Write-Host "Programming $Name with STM32CubeProgrammer..."
  & $CubeProgrammer @connectArgs @writeArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$Name programming failed, exit code $LASTEXITCODE"
  }
}

function Write-HexByDaplink {
  param(
    [string]$Hex,
    [string]$Name,
    [string]$PackPath
  )

  if (!(Test-Path -LiteralPath $Hex)) {
    throw "$Name HEX file not found: $Hex"
  }

  $flashArgs = @("flash", "--target", $Target, "--frequency", $Frequency, "--pack", $PackPath, "--erase", "sector")
  if ($Probe -ne "") {
    $flashArgs += @("--uid", $Probe)
  }
  $flashArgs += $Hex

  Write-Host "Programming $Name with CMSIS-DAP/pyOCD..."
  Write-Host "Target: $Target"
  Write-Host "Pack  : $PackPath"
  Write-Host "Freq  : $Frequency"
  Invoke-PyOcd $flashArgs
  if ($script:PyOcdExitCode -ne 0) {
    throw "$Name pyOCD flash failed, exit code $script:PyOcdExitCode"
  }
}

function Reset-Stlink {
  Write-Host "Requesting software reset..."
  $connectArgs = @("-c", "port=SWD", "mode=$ConnectMode", "reset=SWrst")
  & $CubeProgrammer @connectArgs "-rst"
  if ($LASTEXITCODE -ne 0) {
    throw "Software reset failed, exit code $LASTEXITCODE"
  }

  Write-Host "Requesting core run..."
  & $CubeProgrammer @connectArgs "-run"
  if ($LASTEXITCODE -ne 0) {
    throw "Core run failed, exit code $LASTEXITCODE"
  }
}

function Reset-Daplink {
  param([string]$PackPath)

  Write-Host "Resetting target with CMSIS-DAP/pyOCD..."
  $resetArgs = @("reset", "--target", $Target, "--frequency", $Frequency, "--pack", $PackPath)
  if ($Probe -ne "") {
    $resetArgs += @("--uid", $Probe)
  }
  Invoke-PyOcd $resetArgs
  if ($script:PyOcdExitCode -ne 0) {
    throw "pyOCD reset failed, exit code $script:PyOcdExitCode"
  }
}

if (!(Test-Path -LiteralPath $Uv4)) {
  throw "UV4.exe not found: $Uv4"
}

if (!$NoBuild) {
  if (!$AppOnly) {
    Invoke-KeilBuild -Project $BootProject -Log $BootLog -Name "Bootloader"
  }
  if (!$BootOnly) {
    Invoke-KeilBuild -Project $AppProject -Log $AppLog -Name "Application"
  }
}

$resolvedProbeType = $ProbeType
if ($ProbeType -eq "auto") {
  Write-Host "Detecting programmer..."
  if (Test-CmsisDapProbe) {
    $resolvedProbeType = "daplink"
  } else {
    $resolvedProbeType = "stlink"
  }
  Write-Host "Selected programmer: $resolvedProbeType"
}

if ($resolvedProbeType -eq "daplink") {
  Test-PyOcd
  $packPath = Get-PackPath
  if (!$AppOnly) {
    Write-HexByDaplink -Hex $BootHex -Name "Bootloader at 0x08000000" -PackPath $packPath
  }
  if (!$BootOnly) {
    Write-HexByDaplink -Hex $AppHex -Name "Application at 0x08020000" -PackPath $packPath
  }
  Reset-Daplink -PackPath $packPath
} else {
  if (!(Test-Path -LiteralPath $CubeProgrammer)) {
    throw "STM32_Programmer_CLI.exe not found: $CubeProgrammer"
  }
  if (!$AppOnly) {
    Write-HexByStlink -Hex $BootHex -Name "Bootloader at 0x08000000"
  }
  if (!$BootOnly) {
    Write-HexByStlink -Hex $AppHex -Name "Application at 0x08020000"
  }
  Reset-Stlink
}

Write-Host "Done. Bootloader/application programming flow completed."
