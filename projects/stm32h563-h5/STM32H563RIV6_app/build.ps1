<#
  build.ps1

  Compile-only Keil rebuild entry for the standalone STM32H563 App.
  This script never connects to, erases, resets, or programs target hardware.
#>

param(
  [switch]$Clean
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$uv4 = "C:\Keil_v5\UV4\UV4.exe"
$uvProject = Join-Path $projectRoot "MDK-ARM\STM32H563RIV6_app.uvprojx"
$outputDirectory = Join-Path $projectRoot "MDK-ARM\STM32H563RIV6_app"
$buildLog = Join-Path $projectRoot "MDK-ARM\build_STM32H563RIV6_app.log"

if(!(Test-Path -LiteralPath $uv4)) {
  throw "UV4.exe not found: $uv4"
}
if(!(Test-Path -LiteralPath $uvProject)) {
  throw "Keil project not found: $uvProject"
}

$buildMutex = New-Object System.Threading.Mutex($false, "Local\stm32h563_app_modbus_keil_build")
$ownsBuildMutex = $false

try {
  $ownsBuildMutex = $buildMutex.WaitOne(0)
  if(!$ownsBuildMutex) {
    throw "Another compile-only Keil build is already running for this project."
  }

  if($Clean -and (Test-Path -LiteralPath $outputDirectory)) {
    $resolvedRoot = (Resolve-Path -LiteralPath $projectRoot).Path
    $resolvedOutput = (Resolve-Path -LiteralPath $outputDirectory).Path
    if(!$resolvedOutput.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
      throw "Refusing to remove output outside project: $resolvedOutput"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
  }

  Remove-Item -LiteralPath $buildLog -Force -ErrorAction SilentlyContinue
  $buildStartUtc = [DateTime]::UtcNow

  Write-Host "Rebuilding standalone App (compile only)..."
  # Use bounded parallelism. Unbounded -j0 skipped an object without reporting
  # a compiler error, while -j1 leaves this 700+ source target idle in UV4.
  $process = Start-Process -FilePath $uv4 `
                           -ArgumentList @("-r", "`"$uvProject`"", "-j4", "-o", "`"$buildLog`"") `
                           -WindowStyle Hidden `
                           -Wait `
                           -PassThru

  if(!(Test-Path -LiteralPath $buildLog)) {
    throw "Keil did not create a fresh build log. Exit code: $($process.ExitCode)"
  }

  $logItem = Get-Item -LiteralPath $buildLog
  if($logItem.LastWriteTimeUtc -lt $buildStartUtc.AddSeconds(-2)) {
    throw "Build log is stale: $buildLog"
  }

  $buildText = Get-Content -Raw -LiteralPath $buildLog
  if($buildText -notmatch "0 Error\(s\)") {
    Get-Content -LiteralPath $buildLog -Tail 80
    throw "Keil rebuild failed. Exit code: $($process.ExitCode)"
  }

  Get-Content -LiteralPath $buildLog -Tail 40
  Write-Host "Compile-only rebuild complete. No hardware operation was performed."
}
finally {
  if($ownsBuildMutex) {
    $buildMutex.ReleaseMutex()
  }
  $buildMutex.Dispose()
}
