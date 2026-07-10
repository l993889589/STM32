<#
.SYNOPSIS
Builds and runs host tests for the external firmware A/B transaction.

.DESCRIPTION
Uses a fake NOR image to prove that incomplete or corrupt candidate downloads
leave the active firmware slot untouched and never publish a PENDING record.
#>
param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\ota_firmware_update"
$Executable = Join-Path $BuildDir "test_ota_firmware_ab.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I", (Join-Path $Root "shared\ota"),
    (Join-Path $Root "shared\ota\ota_boot_control.c"),
    (Join-Path $Root "shared\ota\ota_sha256.c"),
    (Join-Path $Root "shared\ota\ota_firmware_update.c"),
    (Join-Path $Root "tools\tests\test_ota_firmware_update.c"),
    "-o", $Executable
)

& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "Host firmware A/B test build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0)
{
    throw "Host firmware A/B tests failed with exit code $ExitCode"
}
if(($Output -join "`n") -notmatch "ota_firmware_update: all tests passed")
{
    throw "Host firmware A/B test executable did not report successful execution"
}
