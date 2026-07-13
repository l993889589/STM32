$ErrorActionPreference = 'Stop'

$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32F767_ld_modbus_baremetal.uvprojx'
$xml = [xml](Get-Content -LiteralPath $project -Raw -Encoding UTF8)
$projectDirectory = Split-Path -Parent $project
$compiled = @{}

foreach($file in $xml.Project.Targets.Target.Groups.Group.Files.File)
{
    $path = [string]$file.FilePath
    if($path)
    {
        $full = [IO.Path]::GetFullPath((Join-Path $projectDirectory $path))
        if(!(Test-Path -LiteralPath $full))
        {
            throw "Missing Keil source: $path"
        }
        if([IO.Path]::GetExtension($full) -eq '.c')
        {
            $compiled[$full.ToLowerInvariant()] = $true
        }
    }
}

$owned = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'app') -Filter *.c
$owned += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\src') -Filter *.c
foreach($file in $owned)
{
    if(!$compiled.ContainsKey($file.FullName.ToLowerInvariant()))
    {
        throw "Project-owned source is not compiled: $($file.FullName)"
    }
}

$projectText = Get-Content -LiteralPath $project -Raw -Encoding UTF8
if($projectText -match '[A-Za-z]:\\')
{
    throw 'Keil project contains an absolute source path'
}
if($projectText -notmatch '<CreateHexFile>0</CreateHexFile>')
{
    throw 'HEX generation must remain disabled for compile-only validation'
}

$heapHits = rg -n --glob '*.c' --glob '*.h' '\b(malloc|calloc|realloc|free)\s*\(' (Join-Path $PSScriptRoot 'app') (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\src')
if($LASTEXITCODE -eq 0)
{
    throw "Runtime heap use found:`n$heapHits"
}

$port = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'app\modbus_port.c') -Raw -Encoding UTF8
if($port -notmatch 'HAL_UART_Receive_IT\(&huart3,\s*&g_uart_rx_byte,\s*1U\)')
{
    throw 'USART3 one-byte interrupt receive entry is missing'
}
if($port -match 'ReceiveToIdle|Receive_DMA|HAL_TIM|DWT')
{
    throw 'Modbus port unexpectedly depends on DMA, IDLE, TIM, or DWT'
}

$bareRtosHits = rg -n --glob '*.c' --glob '*.h' 'FreeRTOS\.h|task\.h' (Join-Path $PSScriptRoot 'app')
if($LASTEXITCODE -eq 0)
{
    throw "Bare-metal application contains FreeRTOS dependencies:`n$bareRtosHits"
}

Write-Output 'project_check: ok'
