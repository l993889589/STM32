param(
    [string]$Port = "COM19",
    [int]$BaudRate = 115200,
    [string]$LogPath = "$PSScriptRoot\..\Logs\uart4.log"
)

$ErrorActionPreference = "Stop"
$resolvedLogPath = [System.IO.Path]::GetFullPath($LogPath)
$logDirectory = [System.IO.Path]::GetDirectoryName($resolvedLogPath)

[System.IO.Directory]::CreateDirectory($logDirectory) | Out-Null
$serialPort = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serialPort.Encoding = [System.Text.Encoding]::ASCII
$serialPort.ReadTimeout = 200
$serialPort.DtrEnable = $false
$serialPort.RtsEnable = $false

try {
    $serialPort.Open()
    [System.IO.File]::AppendAllText(
        $resolvedLogPath,
        "`r`n--- UART logger started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $Port@$BaudRate ---`r`n",
        [System.Text.Encoding]::UTF8
    )

    while ($true) {
        $text = $serialPort.ReadExisting()
        if ($text.Length -gt 0) {
            [System.IO.File]::AppendAllText(
                $resolvedLogPath,
                $text,
                [System.Text.Encoding]::UTF8
            )
        }
        Start-Sleep -Milliseconds 20
    }
}
finally {
    if ($serialPort.IsOpen) {
        $serialPort.Close()
    }
    $serialPort.Dispose()
}
