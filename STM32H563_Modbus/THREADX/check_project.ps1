$ErrorActionPreference = 'Stop'
$violations = New-Object System.Collections.Generic.List[string]

function Require-NoMatch([string]$name, [string]$pattern, [string[]]$paths)
{
    $matches = & rg -n --glob '*.c' --glob '*.h' $pattern @paths 2>$null
    if($LASTEXITCODE -eq 0)
    {
        $violations.Add($name + "`n" + ($matches -join "`n"))
    }
}

$user = Join-Path $PSScriptRoot 'user'
$protocol = Join-Path $PSScriptRoot 'Middlewares\ld_modbus'
$portable_paths = @(
    (Join-Path $PSScriptRoot 'user\bsp'),
    (Join-Path $PSScriptRoot 'user\ldc'),
    (Join-Path $PSScriptRoot 'user\transport'),
    (Join-Path $PSScriptRoot 'user\at'),
    $protocol
)
$documented_files = @()
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'Core\Src') -File -Filter *.c |
    Where-Object Name -ne 'system_stm32h5xx.c'
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'Core\Inc') -File -Filter *.h |
    Where-Object Name -notin 'stm32h5xx_hal_conf.h', 'tx_user.h'
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'user\bsp'),
                                   (Join-Path $PSScriptRoot 'user\app'),
                                   (Join-Path $PSScriptRoot 'user\osal'),
                                   (Join-Path $PSScriptRoot 'user\transport') -File |
    Where-Object Extension -in '.c', '.h'

$definition_pattern = '(?ms)^(?!\s*(?:if|for|while|switch)\b)(?:static\s+)?(?:const\s+)?[A-Za-z_][A-Za-z0-9_\s]*?(?:\*+\s*)?(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*?\)\s*\{'
$declaration_pattern = '(?ms)^(?!\s*(?:if|for|while|switch|typedef)\b)(?:static\s+)?(?:const\s+)?[A-Za-z_][A-Za-z0-9_\s]*?(?:\*+\s*)?(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*?\)\s*;'

foreach($file in $documented_files)
{
    $raw = Get-Content -LiteralPath $file.FullName -Raw
    $header = ($raw -split "\r?\n" | Select-Object -First 12) -join "`n"
    if(($header -notmatch '@file') -or ($header -notmatch '@brief'))
    {
        $violations.Add('Missing file header: ' + $file.FullName)
    }

    $pattern = if($file.Extension -eq '.c') { $definition_pattern } else { $declaration_pattern }
    foreach($match in [regex]::Matches($raw, $pattern))
    {
        $prefix = $raw.Substring(0, $match.Index).TrimEnd()
        if(-not $prefix.EndsWith('*/'))
        {
            $violations.Add('Missing function comment: ' + $file.FullName + ' :: ' + $match.Groups['name'].Value)
        }
    }
}

$public_headers = Get-ChildItem (Join-Path $PSScriptRoot 'user\bsp') -File -Filter *.h |
    Where-Object Name -notmatch '^mcu_.*\.h$' |
    ForEach-Object FullName

Require-NoMatch 'MX dependency' '\bMX_[A-Za-z0-9_]+' @((Join-Path $PSScriptRoot 'Core'), $user)
Require-NoMatch 'ThreadX leaked into portable BSP, LDC, transport, AT, or protocol' '#\s*include\s*[<"]tx_(api|port)\.h[>"]' $portable_paths
Require-NoMatch 'HAL type leaked through public BSP headers' '(GPIO|TIM|UART|SPI|I2C|FDCAN|PCD|RTC)_[A-Za-z0-9_]*TypeDef|stm32h5xx_hal\.h' $public_headers
Require-NoMatch 'Runtime heap allocation' '\b(malloc|calloc|realloc|free)\s*\(' @((Join-Path $PSScriptRoot 'Core'), $user)
Require-NoMatch 'Runtime heap allocation in ld_modbus' '\b(malloc|calloc|realloc|free)\s*\(' @($protocol)
Require-NoMatch 'Legacy BSP naming' 'bsp_(diagnostics|fatal|critical)|bsp_[a-z0-9_]+_stm32h5|drv_(gd25lq128|ft6336|st7796|w800)' @($user)

$project = Join-Path $PSScriptRoot 'MDK-ARM\stm32_h563_modbus_threadx.uvprojx'
$raw = Get-Content -LiteralPath $project -Raw
if(($raw -match '[A-Za-z]:\\') -or ($raw -match '\.\.\\\.\.\\'))
{
    $violations.Add('Keil project contains an external source path')
}

$project_directory = Split-Path -Parent $project
$project_sources = [regex]::Matches($raw, '<FilePath>([^<]+\.c)</FilePath>') |
    ForEach-Object {
        [System.IO.Path]::GetFullPath((Join-Path $project_directory $_.Groups[1].Value))
    }
$owned_source_directories = @(
    (Join-Path $PSScriptRoot 'Core\Src'),
    (Join-Path $PSScriptRoot 'user\app'),
    (Join-Path $PSScriptRoot 'user\at\core'),
    (Join-Path $PSScriptRoot 'user\at\modules'),
    (Join-Path $PSScriptRoot 'user\bsp'),
    (Join-Path $PSScriptRoot 'user\ldc\core'),
    (Join-Path $PSScriptRoot 'user\osal'),
    (Join-Path $PSScriptRoot 'user\transport')
)
$owned_sources = $owned_source_directories | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -File -Filter *.c | ForEach-Object FullName
}
foreach($source in $owned_sources)
{
    if($source -notin $project_sources)
    {
        $violations.Add('Project-owned source is not compiled: ' + $source)
    }
}

$vector_source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Core\Src\system_stm32h5xx.c') -Raw
if($vector_source -notmatch '#define\s+VECT_TAB_OFFSET\s+0x00000U')
{
    $violations.Add('VTOR offset must remain zero for the standalone Modbus image')
}
if(($raw -notmatch '<StartAddress>0x8000000</StartAddress>') -or
   ($raw -notmatch '<Size>0x200000</Size>'))
{
    $violations.Add('Keil standalone flash region must remain 0x08000000..0x081FFFFF')
}
if(($raw -notmatch 'MODBUS_UART_RX_DMA=1,MODBUS_UART_TX_DMA=1') -or
   ([regex]::Matches($raw, '<FileName>modbus_network_app\.c</FileName>')).Count -ne 2)
{
    $violations.Add('ThreadX IT/DMA targets must compile the network service and DMA target must enable RX plus TX DMA')
}
$uart_source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'user\bsp\mcu_uart.c') -Raw
$uart_irq_source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'user\bsp\board_uart.c') -Raw
if(($uart_source -notmatch 'GPDMA1_REQUEST_USART2_TX') -or
   ($uart_irq_source -notmatch 'GPDMA1_Channel2_IRQHandler'))
{
    $violations.Add('USART2 DMA target must own a TX DMA request and IRQ dispatcher')
}
$root_ignore = Get-Content -LiteralPath (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) '.gitignore') -Raw
if($root_ignore -notmatch 'STM32H563_Modbus/THREADX/user/app/modbus_network_config_local\.h')
{
    $violations.Add('Local W800 credentials file must stay ignored by Git')
}
if(($raw -notmatch 'TX_SINGLE_MODE_NON_SECURE=1') -or
   ($raw -notmatch 'BSP_RUNTIME_THREADX=1') -or
   ($raw -match 'osal_bare_metal\.c') -or
   ($raw -match '\.\.\\rtos\\'))
{
    $violations.Add('ThreadX target runtime ownership is incomplete or conflicts with bare metal')
}
if(([regex]::Matches($raw, '<GroupName>Core/System</GroupName>')).Count -ne 2 -or
   ([regex]::Matches($raw, '<GroupName>BSP/(Common|MCU|Board|Device)</GroupName>')).Count -ne 8 -or
   ([regex]::Matches($raw, '<FileName>mcu_hal_timebase\.c</FileName>')).Count -ne 2 -or
   $raw -match '<CreateHexFile>1</CreateHexFile>')
{
    $violations.Add('Keil BSP/Core grouping, HAL timebase ownership, or compile-only HEX policy is incomplete')
}
$threadx_low_level = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Middlewares\ST\threadx\ports\cortex_m33\ac6\src\tx_initialize_low_level.S') -Raw
if(($threadx_low_level -notmatch 'SYSTEM_CLOCK\s*=\s*250000000') -or
   ($threadx_low_level -notmatch 'SYSTEM_CLOCK / 1000'))
{
    $violations.Add('ThreadX SysTick must be derived from the 250 MHz clock at 1 kHz')
}

if($violations.Count -ne 0)
{
    $violations | ForEach-Object { Write-Output $_ }
    exit 1
}

Write-Output 'project_check: ok'
exit 0
