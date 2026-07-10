<#
.SYNOPSIS
Builds and runs host tests for the bootloader OTA v2 decision policy.

.DESCRIPTION
Verifies interrupted-install retry, bounded trial attempts, rollback, confirmed
image repair and migration from version-1 manifests without accessing a board.
#>
param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\ota_boot_v2"
$Executable = Join-Path $BuildDir "test_ota_boot_v2.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I", (Join-Path $Root "tools\tests\stubs"),
    "-I", (Join-Path $Root "shared\ota"),
    "-I", (Join-Path $Root "STM32H563_Bootloader\user"),
    (Join-Path $Root "shared\ota\ota_boot_control.c"),
    (Join-Path $Root "STM32H563_Bootloader\user\ota_boot_v2.c"),
    (Join-Path $Root "tools\tests\test_ota_boot_v2.c"),
    "-o", $Executable
)

& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "Host OTA boot v2 test build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0)
{
    throw "Host OTA boot v2 tests failed with exit code $ExitCode"
}
if(($Output -join "`n") -notmatch "ota_boot_v2: all tests passed")
{
    throw "Host OTA boot v2 test executable did not report successful execution"
}
