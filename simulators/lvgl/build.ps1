param(
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Release",
  [switch]$Run
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$CMake = "C:\Program Files\CMake\bin\cmake.exe"

if (!(Test-Path -LiteralPath $CMake)) {
  $CMake = "cmake"
}

& $CMake -S $Root -B $BuildDir
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed, exit code $LASTEXITCODE"
}

& $CMake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed, exit code $LASTEXITCODE"
}

if ($Run) {
  $Exe = Join-Path $BuildDir "$Config\lvgl_simulator.exe"
  if (!(Test-Path -LiteralPath $Exe)) {
    throw "Simulator executable not found: $Exe"
  }
  & $Exe
}
