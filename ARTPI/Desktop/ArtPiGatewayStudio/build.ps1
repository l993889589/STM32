param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$preset = switch ($Configuration) {
    'Debug' { 'windows-msvc-debug' }
    'RelWithDebInfo' { 'windows-msvc-relwithdebinfo' }
    default { 'windows-msvc-release' }
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vsWhere) {
    $vsRoot = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
} else {
    $vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw 'Visual Studio C++ developer environment was not found.'
}

$buildFlags = if ($Clean) { ' --clean-first' } else { '' }
$commands = "cmake --preset $preset && cmake --build --preset $preset$buildFlags"
if ($Package) {
    if ($Configuration -ne 'Release') {
        throw 'ZIP packaging is supported by the Release preset only.'
    }
    $commands += " && cpack --preset windows-msvc-release"
}

$commandLine = "call `"$vsDevCmd`" -arch=x64 && $commands"
Push-Location $project
try {
    & cmd.exe /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
