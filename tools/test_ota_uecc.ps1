<# Builds and runs the Bootloader's production P-256 verification backend. #>
param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SourceDir = Join-Path $Root "shared\third_party\micro-ecc"
$BuildDir = Join-Path $Root "build\tests\ota_uecc"
$Executable = Join-Path $BuildDir "test_ota_uecc.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas",
    "-DuECC_PLATFORM=0",
    "-DuECC_SUPPORTS_secp160r1=0",
    "-DuECC_SUPPORTS_secp192r1=0",
    "-DuECC_SUPPORTS_secp224r1=0",
    "-DuECC_SUPPORTS_secp256r1=1",
    "-DuECC_SUPPORTS_secp256k1=0",
    "-DuECC_SUPPORT_COMPRESSED_POINT=0",
    "-I", $SourceDir,
    (Join-Path $SourceDir "uECC.c"),
    (Join-Path $Root "tools\tests\test_ota_uecc.c"),
    "-lcrypt32", "-ladvapi32",
    "-o", $Executable
)
& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "OTA micro-ecc host build failed with exit code $LASTEXITCODE"
}

$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0 -or ($Output -join "`n") -notmatch "all tests passed")
{
    throw "OTA micro-ecc tests failed"
}
