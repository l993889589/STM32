$ErrorActionPreference = 'Stop'

$uv4 = 'C:\Keil_v5\UV4\UV4.exe'
$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32H563_Modbus.uvprojx'
$log = Join-Path $PSScriptRoot 'MDK-ARM\build.log'

if(Test-Path -LiteralPath $log)
{
    Remove-Item -LiteralPath $log -Force
}

$arguments = '-r "' + $project + '" -t "STM32H563_Modbus" -j0 -o "' + $log + '"'
$process = Start-Process -FilePath $uv4 -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden

if(!(Test-Path -LiteralPath $log))
{
    throw "Keil did not create build.log (exit code $($process.ExitCode))"
}

$log_text = Get-Content -LiteralPath $log -Raw -Encoding Default
Write-Output $log_text

if($log_text -notmatch ' - 0 Error\(s\), 0 Warning\(s\)\.')
{
    throw 'Keil rebuild failed'
}

& (Join-Path $PSScriptRoot 'verify_image_address.ps1')
