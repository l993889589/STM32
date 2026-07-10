param()

<#
  test_at_core_host.ps1

  Purpose:
    Compile and run the platform-neutral AT Core binary framing regression test
    with the host MinGW GCC toolchain.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_at_core_host.ps1"

  Constraints:
    The executable is written under build/tests and is not a firmware artifact.
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Gcc = (Get-Command gcc -ErrorAction Stop).Source
$AtCore = Join-Path $Root "STM32H563_App\user\at\core"
$TestSource = Join-Path $Root "STM32H563_App\tests\host\test_at_binary.c"
$OutputDir = Join-Path $Root "build\tests"
$Output = Join-Path $OutputDir "test_at_binary.exe"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

& $Gcc `
  -std=c11 `
  -Wall `
  -Wextra `
  -Werror `
  -I $AtCore `
  $TestSource `
  (Join-Path $AtCore "at_core.c") `
  (Join-Path $AtCore "at_session.c") `
  (Join-Path $AtCore "at_urc.c") `
  -o $Output

if($LASTEXITCODE -ne 0) {
  throw "AT Core host test build failed with exit code $LASTEXITCODE."
}

& $Output
if($LASTEXITCODE -ne 0) {
  throw "AT Core host test failed with exit code $LASTEXITCODE."
}
