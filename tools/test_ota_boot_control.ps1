<#
.SYNOPSIS
Builds and runs the host OTA boot-control fault-injection tests.

.DESCRIPTION
Compiles the hardware-independent shared OTA record module with strict warnings
and executes torn-write tests for every byte before the commit marker finishes.
No board, network service or external flash is modified.
#>
param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\ota_boot_control"
$Executable = Join-Path $BuildDir "test_ota_boot_control.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I", (Join-Path $Root "shared\ota"),
    (Join-Path $Root "shared\ota\ota_boot_control.c"),
    (Join-Path $Root "tools\tests\test_ota_boot_control.c"),
    "-o", $Executable
)

& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "Host OTA boot-control test build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0)
{
    throw "Host OTA boot-control tests failed with exit code $ExitCode"
}
if(($Output -join "`n") -notmatch "ota_boot_control: all tests passed")
{
    throw "Host OTA boot-control test executable did not report successful execution"
}
