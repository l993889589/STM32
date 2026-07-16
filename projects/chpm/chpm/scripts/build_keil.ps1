param(
    [string]$KeilRoot = 'C:\Keil_v5',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $root 'MDK-ARM\F4.uvprojx'
$log = Join-Path $root 'MDK-ARM\build.log'
$uv4 = Join-Path $KeilRoot 'UV4\UV4.exe'

if (-not (Test-Path -LiteralPath $uv4)) { throw "Keil uVision not found: $uv4" }
if (-not (Test-Path -LiteralPath $project)) { throw "Project not found: $project" }
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

$operation = if ($Rebuild) { '-r' } else { '-b' }
& $uv4 $operation $project -j0 -o $log

$deadline = [DateTime]::UtcNow.AddMinutes(3)
do {
    if (Test-Path -LiteralPath $log) {
        $text = Get-Content -LiteralPath $log -Raw
        if ($text -match 'Target not created' -or
            $text -match '"[^\"]+"\s+-\s+[1-9][0-9]* Error\(s\)') {
            Get-Content -LiteralPath $log -Tail 80
            throw 'Keil build failed.'
        }
        if ($text -match '"[^\"]+"\s+-\s+0 Error\(s\), 0 Warning\(s\)') {
            Get-Content -LiteralPath $log -Tail 6
            exit 0
        }
    }
    Start-Sleep -Milliseconds 250
} while ([DateTime]::UtcNow -lt $deadline)

throw "Keil build did not finish before timeout. Inspect $log"
