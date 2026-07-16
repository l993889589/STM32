param(
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "work\at-host"
$include = Join-Path $root "user\at\core"
$sources = @(
    (Join-Path $root "user\at\core\at_core.c"),
    (Join-Path $root "user\at\core\at_session.c"),
    (Join-Path $root "user\at\core\at_urc.c")
)
$tests = @(
    "test_at_binary",
    "test_at_resilience"
)

New-Item -ItemType Directory -Force -Path $output | Out-Null

foreach($test in $tests) {
    $source = Join-Path $root "tests\host\$test.c"
    $binary = Join-Path $output "$test.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -pedantic "-I$include" $source $sources -o $binary
    if($LASTEXITCODE -ne 0) {
        throw "AT host test compile failed: $test"
    }
    & $binary
    if($LASTEXITCODE -ne 0) {
        throw "AT host test failed: $test"
    }
}

Write-Host "AT host tests passed. No hardware operation was performed."
