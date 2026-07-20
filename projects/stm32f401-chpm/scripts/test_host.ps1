param([string]$BuildRoot)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $root 'work\host-tests'
}
$paramBuild = Join-Path $BuildRoot 'param'
$dwinBuild = Join-Path $BuildRoot 'dwin-protocol'
$sensorsBuild = Join-Path $BuildRoot 'sensors'
$ldcBuild = Join-Path $BuildRoot 'ldc'
$modbusBuild = Join-Path $BuildRoot 'modbus'

cmake -S (Join-Path $root 'tests\host') -B $paramBuild
cmake --build $paramBuild --config Release
ctest --test-dir $paramBuild -C Release --output-on-failure

cmake -S (Join-Path $root 'third_party\dwin_protocol') -B $dwinBuild `
    -DDWIN_PROTOCOL_BUILD_TESTS=ON
cmake --build $dwinBuild --config Release
ctest --test-dir $dwinBuild -C Release --output-on-failure

cmake -S (Join-Path $root 'third_party\sensors') -B $sensorsBuild `
    -DSENSORS_BUILD_TESTS=ON
cmake --build $sensorsBuild --config Release
ctest --test-dir $sensorsBuild -C Release --output-on-failure

cmake -S (Join-Path $root 'tests\ldc') -B $ldcBuild
cmake --build $ldcBuild --config Release
ctest --test-dir $ldcBuild -C Release --output-on-failure

cmake -S (Join-Path $root 'third_party\ld_modbus') -B $modbusBuild `
    -DLD_MODBUS_BUILD_TESTS=ON
cmake --build $modbusBuild --config Release
ctest --test-dir $modbusBuild -C Release --output-on-failure
