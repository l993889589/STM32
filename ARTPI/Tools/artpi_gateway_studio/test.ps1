param(
    [string]$Endpoint = 'http://192.168.1.20',
    [switch]$SkipHardware
)

$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$exe = Join-Path $project 'build\release\artpi_gateway_studio.exe'
$qtBin = 'D:\Qt\6.8.3\msvc2022_64\bin'
$env:Path = "$qtBin;$env:Path"

if (-not (Test-Path -LiteralPath $exe)) {
    throw 'Release executable not found. Run .\build.ps1 -Clean first.'
}

function Invoke-GatewayStudio {
    param([string]$Arguments, [int]$TimeoutMs = 20000)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $exe
    $startInfo.Arguments = $Arguments
    $startInfo.UseShellExecute = $false
    $process = [Diagnostics.Process]::Start($startInfo)
    if (-not $process.WaitForExit($TimeoutMs)) {
        $process.Kill()
        throw "Timed out: $Arguments"
    }
    if ($process.ExitCode -ne 0) {
        throw "Failed with exit code $($process.ExitCode): $Arguments"
    }
}

if (-not $SkipHardware) {
    Invoke-GatewayStudio "--self-test $Endpoint"
    Invoke-GatewayStudio "--integration-test $Endpoint"
}
Invoke-GatewayStudio '--industrial-self-test'

$capture = Join-Path $project 'build\mqtt_capture.json'
Remove-Item -LiteralPath $capture -ErrorAction SilentlyContinue
$broker = Start-Process -FilePath (Get-Command python.exe).Source `
    -ArgumentList @((Join-Path $project 'tests\mqtt_test_broker.py'), $capture) `
    -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 350
    Invoke-GatewayStudio '--mqtt-self-test' 10000
    if (-not $broker.WaitForExit(10000)) {
        $broker.Kill()
        throw 'MQTT loopback broker timed out.'
    }
    if ($broker.ExitCode -ne 0) {
        throw "MQTT loopback broker failed with exit code $($broker.ExitCode)."
    }
    $mqtt = Get-Content -LiteralPath $capture -Raw -Encoding utf8 | ConvertFrom-Json
    if ($mqtt.topic -ne 'artpi/self-test' -or $mqtt.payload.board -ne 'ART-Pi MQTT Test') {
        throw 'MQTT publish payload validation failed.'
    }
} finally {
    if (-not $broker.HasExited) { $broker.Kill() }
}

$validation = Join-Path $project 'build\validation'
New-Item -ItemType Directory -Path $validation -Force | Out-Null
foreach ($section in 0..4) {
    $image = Join-Path $validation "industrial_section_$section.png"
    Invoke-GatewayStudio "--page 4 --industrial-section $section --screenshot `"$image`"" 15000
    if ((Get-Item -LiteralPath $image).Length -lt 10000) {
        throw "Invalid UI screenshot: $image"
    }
}

Write-Host 'ART-Pi Gateway Studio validation passed.' -ForegroundColor Green
