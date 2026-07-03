param(
    [string]$Port = "COM19",
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"

$serial = New-Object System.IO.Ports.SerialPort $Port,$BaudRate,'None',8,'One'
$serial.ReadTimeout = 500
$serial.WriteTimeout = 500

try {
    $serial.Open()
} catch {
    Write-Host "UART4 log port $Port is busy or unavailable. Leave it to the active terminal."
    exit 2
}

try {
    Write-Host "Watching UART4 log on $Port. Press Ctrl+C to stop."
    while($true) {
        try {
            $text = $serial.ReadExisting()
            if($text.Length -gt 0) {
                Write-Host -NoNewline $text
            }
        } catch [TimeoutException] {
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    if($serial.IsOpen) {
        $serial.Close()
    }
}
