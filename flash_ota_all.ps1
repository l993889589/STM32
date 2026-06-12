param(
  [switch]$NoBuild,
  [switch]$NoVerify,
  [string]$ConnectMode = "HotPlug"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$CubeProgrammer = "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

$BootProject = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BootLog = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\build_bootloader_flash_all.log"
$BootHex = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Bootloader\STM32H563_Bootloader.hex"

$AppProject = Join-Path $Root "STM32H563_Threadx_usbx_cdc_acm\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$AppLog = Join-Path $Root "STM32H563_Threadx_usbx_cdc_acm\MDK-ARM\build_app_flash_all.log"
$AppHex = Join-Path $Root "STM32H563_Threadx_usbx_cdc_acm\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"

function Invoke-KeilBuild {
  param(
    [string]$Project,
    [string]$Log,
    [string]$Name
  )

  Write-Host "Building $Name..."
  & $Uv4 -r $Project -j0 -o $Log
  $exitCode = $LASTEXITCODE

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

function Write-Hex {
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

  Write-Host "Programming $Name..."
  & $CubeProgrammer @connectArgs @writeArgs
  if ($LASTEXITCODE -ne 0) {
    throw "$Name programming failed, exit code $LASTEXITCODE"
  }
}

if (!(Test-Path -LiteralPath $Uv4)) {
  throw "UV4.exe not found: $Uv4"
}

if (!(Test-Path -LiteralPath $CubeProgrammer)) {
  throw "STM32_Programmer_CLI.exe not found: $CubeProgrammer"
}

if (!$NoBuild) {
  Invoke-KeilBuild -Project $BootProject -Log $BootLog -Name "Bootloader"
  Invoke-KeilBuild -Project $AppProject -Log $AppLog -Name "Application"
}

Write-Hex -Hex $BootHex -Name "Bootloader at 0x08000000"
Write-Hex -Hex $AppHex -Name "Application at 0x08020000"

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

Write-Host "Done. Bootloader and relocated app were programmed, then software reset/run was requested."
