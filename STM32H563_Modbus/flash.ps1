param(
    [switch]$Build,
    [switch]$BuildOnly,
    [switch]$List,
    [ValidateSet('IT', 'DMA')]
    [string]$Variant = 'IT',
    [ValidateSet('BareMetal', 'ThreadX')]
    [string]$Runtime = 'BareMetal',
    [string]$Target = 'stm32h563rivx',
    [string]$Probe = '',
    [string]$Frequency = '1000000',
    [string]$Pack = ''
)

$ErrorActionPreference = 'Stop'

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$localPython = Join-Path $workspaceRoot 'tools\python-3.12.4-embed-amd64\python.exe'
$defaultPack = Join-Path $workspaceRoot 'tools\cmsis-packs\Keil.STM32H5xx_DFP.1.2.0.pack'
$imageName = if($Runtime -eq 'ThreadX')
{
    if($Variant -eq 'DMA') { 'STM32H563_Modbus_ThreadX_DMA' }
    else { 'STM32H563_Modbus_ThreadX_IT' }
}
else
{
    if($Variant -eq 'DMA') { 'STM32H563_Modbus_DMA' }
    else { 'STM32H563_Modbus' }
}
$hex = Join-Path $PSScriptRoot ("MDK-ARM\{0}\{0}.hex" -f $imageName)
$script:PyOcdExitCode = 0

function Invoke-PyOcd
{
    param([string[]]$Arguments)

    if(Test-Path -LiteralPath $localPython)
    {
        & $localPython -m pyocd @Arguments
    }
    else
    {
        & python -m pyocd @Arguments
    }
    $script:PyOcdExitCode = if($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
}

function Get-PackPath
{
    if($Pack -ne '')
    {
        if(!(Test-Path -LiteralPath $Pack))
        {
            throw "CMSIS pack not found: $Pack"
        }
        return (Resolve-Path -LiteralPath $Pack).Path
    }
    if(Test-Path -LiteralPath $defaultPack)
    {
        return (Resolve-Path -LiteralPath $defaultPack).Path
    }
    $cachedPack = Join-Path $env:LOCALAPPDATA 'Arm\Packs\.Download\Keil.STM32H5xx_DFP.1.2.0.pack'
    if(Test-Path -LiteralPath $cachedPack)
    {
        return (Resolve-Path -LiteralPath $cachedPack).Path
    }
    throw 'Keil.STM32H5xx_DFP.1.2.0.pack is required for pyOCD'
}

Invoke-PyOcd @('--version')
if($script:PyOcdExitCode -ne 0)
{
    throw 'pyOCD is unavailable'
}
if($List)
{
    Invoke-PyOcd @('list', '--probes')
    exit $script:PyOcdExitCode
}

if($Build)
{
    $buildScript = if($Runtime -eq 'ThreadX') { 'build_threadx.ps1' } else { 'build.ps1' }
    & (Join-Path $PSScriptRoot $buildScript) -Variant $Variant
}
else
{
    & (Join-Path $PSScriptRoot 'verify_image_address.ps1') -TargetName $imageName
}
if($BuildOnly)
{
    if(!$Build) { throw '-BuildOnly requires -Build' }
    return
}
if(!(Test-Path -LiteralPath $hex))
{
    throw "HEX image not found: $hex"
}

$packPath = Get-PackPath
$probeArgs = if($Probe -ne '') { @('--uid', $Probe) } else { @() }
Write-Output 'H7-TOOL standalone programming contract:'
Write-Output "  Image : $hex"
Write-Output '  Flash : chip erase; image starts at 0x08000000'

Invoke-PyOcd (@('load', '--target', $Target, '--frequency', $Frequency,
                  '--pack', $packPath, '--connect', 'under-reset',
                  '--erase', 'chip') + $probeArgs + @($hex))
if($script:PyOcdExitCode -ne 0)
{
    throw "H7-TOOL standalone program/verify failed: $script:PyOcdExitCode"
}

Write-Output 'flash: H7-TOOL chip erase, program, verify, and reset completed'
