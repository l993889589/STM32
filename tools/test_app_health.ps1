<#
.SYNOPSIS
Builds and runs host tests for OTA application health confirmation gates.
#>
param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\app_health"
$Executable = Join-Path $BuildDir "test_app_health.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I", (Join-Path $Root "tools\tests\stubs"),
    "-I", (Join-Path $Root "STM32H563_App\user\app"),
    (Join-Path $Root "STM32H563_App\user\app\app_health.c"),
    (Join-Path $Root "tools\tests\test_app_health.c"),
    "-o", $Executable
)

& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "Host app-health test build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0)
{
    throw "Host app-health tests failed with exit code $ExitCode"
}
if(($Output -join "`n") -notmatch "app_health: all tests passed")
{
    throw "Host app-health test executable did not report successful execution"
}
