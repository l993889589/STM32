$ErrorActionPreference = 'Stop'
$violations = New-Object System.Collections.Generic.List[string]
$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32H563_CubeMX_Modbus_Loopback.uvprojx'
$ioc = Join-Path $PSScriptRoot 'STM32H563_CubeMX_Modbus_Loopback.ioc'

if(!(Test-Path -LiteralPath $project))
{
    $violations.Add('Missing Keil project: ' + $project)
}

$project_text = Get-Content -LiteralPath $project -Raw
if(($project_text -match '[A-Za-z]:\\') -or ($project_text -match '\.\.\\\.\.\\'))
{
    $violations.Add('Keil project contains an external source path')
}

$ioc_text = Get-Content -LiteralPath $ioc -Raw
foreach($required in @(
    'PA0.Signal=UART4_TX',
    'PA1.Signal=UART4_RX',
    'PA2.Signal=USART2_TX',
    'PA3.Signal=USART2_RX',
    'TIM2.Prescaler=249',
    'TIM2.Prescaler.Ext=249',
    'TIM2.PeriodNoDither=4294967295'
))
{
    if($ioc_text -notmatch [regex]::Escape($required))
    {
        $violations.Add('CubeMX configuration is missing: ' + $required)
    }
}

$project_directory = Split-Path -Parent $project
$compiled_sources = [regex]::Matches($project_text, '<FilePath>([^<]+\.c)</FilePath>') |
    ForEach-Object {
        [System.IO.Path]::GetFullPath((Join-Path $project_directory $_.Groups[1].Value))
    }

$owned_sources = @()
$owned_sources += Get-ChildItem (Join-Path $PSScriptRoot 'App') -File -Filter *.c |
    ForEach-Object FullName
$owned_sources += Get-ChildItem (Join-Path $PSScriptRoot 'Tests') -File -Filter *.c |
    ForEach-Object FullName
$owned_sources += Get-ChildItem (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\src') -File -Filter *.c |
    ForEach-Object FullName

foreach($source in $owned_sources)
{
    if($source -notin $compiled_sources)
    {
        $violations.Add('Project-owned source is not compiled: ' + $source)
    }
}

$app_files = Get-ChildItem (Join-Path $PSScriptRoot 'App') -File |
    Where-Object Extension -in '.c', '.h'
$app_files += Get-ChildItem (Join-Path $PSScriptRoot 'Tests') -File |
    Where-Object Extension -in '.c', '.h'
foreach($file in $app_files)
{
    $text = Get-Content -LiteralPath $file.FullName -Raw
    $header = ($text -split "\r?\n" | Select-Object -First 12) -join "`n"
    if(($header -notmatch '@file') -or ($header -notmatch '@brief'))
    {
        $violations.Add('Missing file header: ' + $file.FullName)
    }
}

$heap_matches = & rg -n --glob '*.c' --glob '*.h' '\b(malloc|calloc|realloc|free)\s*\(' `
    (Join-Path $PSScriptRoot 'App') `
    (Join-Path $PSScriptRoot 'Middlewares\ld_modbus\src') 2>$null
if($LASTEXITCODE -eq 0)
{
    $violations.Add("Runtime heap allocation found:`n" + ($heap_matches -join "`n"))
}

if($project_text -match 'ld_modbus_ldc\.c')
{
    $violations.Add('CubeMX example must not compile the optional LDC adapter')
}

if($violations.Count -ne 0)
{
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'project_check: ok'
exit 0
