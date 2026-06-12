param(
  [switch]$Build,
  [switch]$NoVerify,
  [string]$ConnectMode = "HotPlug"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$CubeProgrammer = "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$UvProject = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BuildLog = Join-Path $ProjectRoot "MDK-ARM\build_from_flash_script.log"
$Hex = Join-Path $ProjectRoot "MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"

if (!(Test-Path -LiteralPath $CubeProgrammer)) {
  throw "STM32_Programmer_CLI.exe not found: $CubeProgrammer"
}

if ($Build) {
  if (!(Test-Path -LiteralPath $Uv4)) {
    throw "UV4.exe not found: $Uv4"
  }

  Write-Host "Building Keil project..."
  & $Uv4 -r $UvProject -j0 -o $BuildLog
  $buildExit = $LASTEXITCODE

  if (Test-Path -LiteralPath $BuildLog) {
    Get-Content -LiteralPath $BuildLog -Tail 40
  }

  $buildOk = $false
  if (Test-Path -LiteralPath $BuildLog) {
    $buildOk = (Select-String -LiteralPath $BuildLog -Pattern "0 Error\(s\)" -Quiet)
  }

  if (($buildExit -ne 0) -and !$buildOk) {
    throw "Keil build failed, exit code $buildExit"
  }

  if (($buildExit -ne 0) -and $buildOk) {
    Write-Host "Keil returned exit code $buildExit, but build log reports 0 errors. Continuing..."
  }
}

if (!(Test-Path -LiteralPath $Hex)) {
  throw "HEX file not found: $Hex. Run with -Build first."
}

$connectArgs = @("-c", "port=SWD", "mode=$ConnectMode", "reset=SWrst")
$writeArgs = @("-w", $Hex)

if (!$NoVerify) {
  $writeArgs += "-v"
}

Write-Host "Programming HEX..."
& $CubeProgrammer @connectArgs @writeArgs
if ($LASTEXITCODE -ne 0) {
  throw "Programming failed, exit code $LASTEXITCODE"
}

Write-Host "Requesting software reset..."
& $CubeProgrammer @connectArgs "-rst"
if ($LASTEXITCODE -ne 0) {
  throw "Software reset failed, exit code $LASTEXITCODE"
}

Write-Host "Requesting core run..."
& $CubeProgrammer @connectArgs "-run"
if ($LASTEXITCODE -ne 0) {
  throw "Core run failed, exit code $LASTEXITCODE"
}

Write-Host "Done. The board should reboot/run without an NRST wire."
