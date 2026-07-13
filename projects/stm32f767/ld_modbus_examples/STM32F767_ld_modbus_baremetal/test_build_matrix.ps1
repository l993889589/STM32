$ErrorActionPreference = 'Stop'

$configPath = Join-Path $PSScriptRoot 'app\modbus_app_config.h'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$originalBytes = [IO.File]::ReadAllBytes($configPath)
$utf8 = [Text.UTF8Encoding]::new($false)

try
{
    $originalText = $utf8.GetString($originalBytes)
    foreach($role in @('SLAVE', 'MASTER'))
    {
        foreach($baud in @(9600, 19200, 115200))
        {
            $configured = [regex]::Replace(
                $originalText,
                '(?m)^#define MODBUS_APP_ROLE MODBUS_APP_ROLE_(SLAVE|MASTER)$',
                "#define MODBUS_APP_ROLE MODBUS_APP_ROLE_$role")
            $configured = [regex]::Replace(
                $configured,
                '(?m)^#define MODBUS_APP_BAUD_RATE \([0-9]+U\)$',
                "#define MODBUS_APP_BAUD_RATE ($($baud)U)")
            [IO.File]::WriteAllText($configPath, $configured, $utf8)

            $output = (& $buildScript 2>&1 | Out-String)
            $size = [regex]::Match($output, 'Program Size:[^\r\n]+').Value
            if(!$size)
            {
                throw "Missing size output for role=$role baud=$baud"
            }
            Write-Output "matrix: role=$role baud=$baud $size"
        }
    }
}
finally
{
    [IO.File]::WriteAllBytes($configPath, $originalBytes)
}

Write-Output 'build_matrix: 6/6 configurations passed'
