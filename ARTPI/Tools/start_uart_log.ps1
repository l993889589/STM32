param(
    [string]$Port = "COM19",
    [int]$BaudRate = 115200
)

$loggerScript = Join-Path $PSScriptRoot "uart_log.ps1"
$logPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Logs\uart4.log"))
$escapedScript = $loggerScript.Replace("'", "''")
$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -like "*$escapedScript*" -and $_.Name -match "powershell"
}

if ($existing) {
    Write-Output "UART logger is already running. PID=$($existing.ProcessId -join ',')"
} else {
    $process = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$loggerScript`"",
        "-Port", $Port,
        "-BaudRate", $BaudRate,
        "-LogPath", "`"$logPath`""
    )
    Write-Output "UART logger started. PID=$($process.Id)"
}

Write-Output "Live view command:"
Write-Output "Get-Content -LiteralPath '$logPath' -Tail 100 -Wait"
