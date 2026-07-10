<#
.SYNOPSIS
Builds and runs host tests for Boot CDC LDOT firmware recovery.

.DESCRIPTION
Compiles the production parser with fake NOR callbacks and verifies fragmented
and coalesced frames, first-slot provisioning, full CRC, PENDING and reset.
#>
param()

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\boot_usb_recovery"
$Executable = Join-Path $BuildDir "test_boot_usb_recovery.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DBOOT_USB_RECOVERY_HOST_TEST",
    "-I", (Join-Path $Root "shared\ota"),
    "-I", (Join-Path $Root "STM32H563_Bootloader\user"),
    (Join-Path $Root "shared\ota\ota_boot_control.c"),
    (Join-Path $Root "shared\ota\ota_sha256.c"),
    (Join-Path $Root "shared\ota\ota_firmware_update.c"),
    (Join-Path $Root "STM32H563_Bootloader\user\boot_usb_recovery.c"),
    (Join-Path $Root "tools\tests\test_boot_usb_recovery.c"),
    "-o", $Executable
)

& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "Boot USB recovery host build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0)
{
    throw "Boot USB recovery tests failed with exit code $ExitCode"
}
if(($Output -join "`n") -notmatch "boot_usb_recovery: all tests passed")
{
    throw "Boot USB recovery test did not report successful execution"
}
