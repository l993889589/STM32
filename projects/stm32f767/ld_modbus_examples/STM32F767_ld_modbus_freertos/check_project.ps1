$ErrorActionPreference = 'Stop'

$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32F767_ld_modbus_freertos.uvprojx'
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
$owned += Get-Item -LiteralPath (Join-Path $PSScriptRoot 'Middlewares\Third_Party\FreeRTOS\Source\tasks.c')
$owned += Get-Item -LiteralPath (Join-Path $PSScriptRoot 'Middlewares\Third_Party\FreeRTOS\Source\list.c')
$owned += Get-Item -LiteralPath (Join-Path $PSScriptRoot 'Middlewares\Third_Party\FreeRTOS\Source\portable\GCC\ARM_CM7\r0p1\port.c')
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

$freeRtosConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Core\Inc\FreeRTOSConfig.h') -Raw -Encoding UTF8
if($freeRtosConfig -notmatch 'configSUPPORT_STATIC_ALLOCATION\s+1' -or
   $freeRtosConfig -notmatch 'configSUPPORT_DYNAMIC_ALLOCATION\s+0' -or
   $freeRtosConfig -notmatch 'configTICK_RATE_HZ\s+\(\(TickType_t\)1000U\)')
{
    throw 'FreeRTOS must use static allocation and a 1 kHz SysTick'
}
if($projectText -match 'heap_[1-5]\.c')
{
    throw 'A dynamic FreeRTOS heap implementation is unexpectedly compiled'
}

Write-Output 'project_check: ok'
