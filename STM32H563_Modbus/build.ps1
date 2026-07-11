param(
    [ValidateSet('IT', 'DMA', 'All')]
    [string]$Variant = 'All'
)

$ErrorActionPreference = 'Stop'

$uv4 = 'C:\Keil_v5\UV4\UV4.exe'
$project = Join-Path $PSScriptRoot 'MDK-ARM\STM32H563_Modbus.uvprojx'
$targets = switch($Variant)
{
    'IT'  { @('STM32H563_Modbus') }
    'DMA' { @('STM32H563_Modbus_DMA') }
    default { @('STM32H563_Modbus', 'STM32H563_Modbus_DMA') }
}

foreach($target in $targets)
{
    $log = Join-Path $PSScriptRoot ("MDK-ARM\build_{0}.log" -f $target)
    if(Test-Path -LiteralPath $log)
    {
        Remove-Item -LiteralPath $log -Force
    }

    $arguments = '-r "' + $project + '" -t "' + $target + '" -j0 -o "' + $log + '"'
    $process = Start-Process -FilePath $uv4 -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden

    if(!(Test-Path -LiteralPath $log))
    {
        throw "Keil did not create $log (exit code $($process.ExitCode))"
    }

    $log_text = Get-Content -LiteralPath $log -Raw -Encoding Default
    Write-Output $log_text

    if($log_text -notmatch ' - 0 Error\(s\), 0 Warning\(s\)\.')
    {
        throw "Keil rebuild failed for $target"
    }

    & (Join-Path $PSScriptRoot 'verify_image_address.ps1') -TargetName $target
}
