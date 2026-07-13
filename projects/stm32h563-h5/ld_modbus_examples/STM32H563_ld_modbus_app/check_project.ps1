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
$documented_files = @()
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'Core\Src') -File -Filter *.c |
    Where-Object Name -ne 'system_stm32h5xx.c'
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'Core\Inc') -File -Filter *.h |
    Where-Object Name -ne 'stm32h5xx_hal_conf.h'
$documented_files += Get-ChildItem (Join-Path $PSScriptRoot 'user\bsp'),
                                   (Join-Path $PSScriptRoot 'user\osal'),
                                   (Join-Path $PSScriptRoot 'user\app') -File |
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
Require-NoMatch 'ThreadX dependency in bare-metal project' 'tx_api\.h|tx_port\.h' @($user)
Require-NoMatch 'HAL type leaked through public BSP headers' '(GPIO|TIM|UART|SPI|I2C|FDCAN|PCD|RTC)_[A-Za-z0-9_]*TypeDef|stm32h5xx_hal\.h' $public_headers
Require-NoMatch 'Runtime heap allocation' '\b(malloc|calloc|realloc|free)\s*\(' @((Join-Path $PSScriptRoot 'Core'), $user, (Join-Path $PSScriptRoot 'Middlewares\ld_modbus'))

$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32H563_ld_modbus_app.uvprojx'
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
    (Join-Path $PSScriptRoot 'user\bsp'),
    (Join-Path $PSScriptRoot 'user\app'),
    (Join-Path $PSScriptRoot 'user\ldc\core'),
    (Join-Path $PSScriptRoot 'user\osal'),
    (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\src'),
    (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\integrations\ldc')
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

if($violations.Count -ne 0)
{
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'project_check: ok'
exit 0
