param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

function Invoke-TestSuite {
    param(
        [string]$Name,
        [string]$Source
    )

    $Build = Join-Path $Root "build\host-tests-$Name"
    Write-Host "Configuring $Name tests..."
    & cmake -S $Source -B $Build -G "Visual Studio 17 2022" -A x64
    if($LASTEXITCODE -ne 0) { throw "$Name configure failed" }

    Write-Host "Building $Name tests..."
    & cmake --build $Build --config $Configuration
    if($LASTEXITCODE -ne 0) { throw "$Name build failed" }

    Write-Host "Running $Name tests..."
    & ctest --test-dir $Build -C $Configuration --output-on-failure
    if($LASTEXITCODE -ne 0) { throw "$Name tests failed" }
}

Invoke-TestSuite -Name "ldc" -Source (Join-Path $Root "tests\ldc")
Invoke-TestSuite -Name "at" -Source (Join-Path $Root "tests\at")

Write-Host "All host communication tests passed."
