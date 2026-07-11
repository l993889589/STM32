param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot 'build\host-tests'
$output = Join-Path $outputDir 'test_ldc_serial_timing.exe'

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

& gcc -std=c99 -Wall -Wextra -Wpedantic -Werror `
    -I (Join-Path $projectRoot 'user\ldc\core') `
    (Join-Path $PSScriptRoot 'test_ldc_serial_timing.c') `
    (Join-Path $projectRoot 'user\ldc\core\ldc_core.c') `
    (Join-Path $projectRoot 'user\ldc\core\ldc_ring.c') `
    (Join-Path $projectRoot 'user\ldc\core\ldc_packet.c') `
    -o $output
if($LASTEXITCODE -ne 0)
{
    throw "timing host build failed: $LASTEXITCODE"
}

& $output
if($LASTEXITCODE -ne 0)
{
    throw "timing host test failed: $LASTEXITCODE"
}
