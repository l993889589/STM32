<# Builds and runs host tests for signed descriptor and anti-rollback policy. #>
param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\tests\ota_security_policy"
$Executable = Join-Path $BuildDir "test_ota_security_policy.exe"
$Gcc = (Get-Command gcc -ErrorAction Stop).Source
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$Arguments = @(
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-I", (Join-Path $Root "shared\ota"),
    (Join-Path $Root "shared\ota\ota_sha256.c"),
    (Join-Path $Root "shared\ota\ota_security_policy.c"),
    (Join-Path $Root "tools\tests\test_ota_security_policy.c"),
    "-o", $Executable
)
& $Gcc @Arguments
if($LASTEXITCODE -ne 0)
{
    throw "OTA security policy host build failed with exit code $LASTEXITCODE"
}
$Output = & $Executable 2>&1
$ExitCode = $LASTEXITCODE
$Output | ForEach-Object { Write-Host $_ }
if($ExitCode -ne 0 -or ($Output -join "`n") -notmatch "all tests passed")
{
    throw "OTA security policy tests failed"
}
