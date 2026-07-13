$ErrorActionPreference = 'Stop'

$uv4 = 'C:\Keil_v5\UV4\UV4.exe'
$target = 'STM32F767_ld_modbus_freertos'
$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32F767_ld_modbus_freertos.uvprojx'
$log = Join-Path $PSScriptRoot 'MDK-ARM\build.log'

if(!(Test-Path -LiteralPath $uv4))
{
    throw "Keil UV4 not found: $uv4"
}

if(Test-Path -LiteralPath $log)
{
    Remove-Item -LiteralPath $log -Force
}

$arguments = '-r "' + $project + '" -t "' + $target + '" -j0 -o "' + $log + '"'
$process = Start-Process -FilePath $uv4 -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden

if(!(Test-Path -LiteralPath $log))
{
    throw "Keil did not create build.log (exit code $($process.ExitCode))"
}

$text = Get-Content -LiteralPath $log -Raw -Encoding Default
Write-Output $text

if($text -notmatch [regex]::Escape("Rebuild target '$target'"))
{
    throw "Keil log does not belong to target '$target'"
}

if($text -notmatch '0 Error\(s\), 0 Warning\(s\)')
{
    throw 'Keil rebuild failed or produced warnings'
}
