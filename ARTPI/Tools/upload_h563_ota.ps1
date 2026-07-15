param(
    [string]$Gateway = "192.168.1.20",
    [Parameter(Mandatory = $true)]
    [string]$Image,
    [Parameter(Mandatory = $true)]
    [string]$Manifest,
    [ValidateRange(1, 247)]
    [int]$UnitId = 1,
    [ValidateSet(0, 115200, 230400, 460800, 921600)]
    [int]$BaudRate = 921600,
    [ValidateRange(256, 7168)]
    [int]$ChunkSize = 4096,
    [switch]$UploadOnly
)

$ErrorActionPreference = "Stop"

$imagePath = (Resolve-Path -LiteralPath $Image).Path
$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
$imageBytes = [IO.File]::ReadAllBytes($imagePath)
$manifestBytes = [IO.File]::ReadAllBytes($manifestPath)

if ($manifestBytes.Length -ne 188) {
    throw "Signed H563 manifest must be exactly 188 bytes: $manifestPath"
}
$manifestImageSize = [BitConverter]::ToUInt32($manifestBytes, 12)
if ($manifestImageSize -ne $imageBytes.Length) {
    throw "Manifest image_size=$manifestImageSize differs from file size=$($imageBytes.Length)"
}

Add-Type -AssemblyName System.Net.Http
$client = [Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(45)
$baseUri = "http://$Gateway"

function Invoke-OtaBinaryPost {
    param(
        [string]$Path,
        [byte[]]$Bytes
    )

    $content = [Net.Http.ByteArrayContent]::new($Bytes)
    $content.Headers.ContentType = [Net.Http.Headers.MediaTypeHeaderValue]::new(
        "application/octet-stream"
    )
    $response = $client.PostAsync("$baseUri$Path", $content).GetAwaiter().GetResult()
    $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    if (-not $response.IsSuccessStatusCode) {
        throw "POST $Path failed: HTTP $([int]$response.StatusCode) $text"
    }
    return $text | ConvertFrom-Json
}

try {
    Write-Host "Uploading signed manifest..."
    $null = Invoke-OtaBinaryPost -Path "/api/ota/h563/manifest" -Bytes $manifestBytes

    for ($offset = 0; $offset -lt $imageBytes.Length; $offset += $ChunkSize) {
        $length = [Math]::Min($ChunkSize, $imageBytes.Length - $offset)
        $chunk = [byte[]]::new($length)
        [Array]::Copy($imageBytes, $offset, $chunk, 0, $length)
        $reply = Invoke-OtaBinaryPost -Path "/api/ota/h563/chunk?offset=$offset" -Bytes $chunk
        $expectedOffset = $offset + $length
        if ([uint32]$reply.next_offset -ne $expectedOffset) {
            throw "Gateway returned next_offset=$($reply.next_offset), expected $expectedOffset"
        }
        $percent = [Math]::Floor(100.0 * $expectedOffset / $imageBytes.Length)
        Write-Progress -Activity "H563 package cache upload" -Status "$expectedOffset / $($imageBytes.Length) bytes" -PercentComplete $percent
    }
    Write-Progress -Activity "H563 package cache upload" -Completed

    Write-Host "Verifying cached image CRC32..."
    $empty = [Net.Http.ByteArrayContent]::new([byte[]]::new(0))
    $finishResponse = $client.PostAsync(
        "$baseUri/api/ota/h563/finish", $empty
    ).GetAwaiter().GetResult()
    $finishText = $finishResponse.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    if (-not $finishResponse.IsSuccessStatusCode) {
        throw "Cache verification failed: HTTP $([int]$finishResponse.StatusCode) $finishText"
    }

    if ($UploadOnly) {
        Write-Host "Signed package is cached and verified; transfer was not started."
        return
    }

    $startJson = "{`"unit_id`":$UnitId,`"baud_rate`":$BaudRate}"
    $startContent = [Net.Http.StringContent]::new(
        $startJson,
        [Text.Encoding]::UTF8,
        "application/json"
    )
    $startResponse = $client.PostAsync(
        "$baseUri/api/ota/h563/start", $startContent
    ).GetAwaiter().GetResult()
    $startText = $startResponse.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    if (-not $startResponse.IsSuccessStatusCode) {
        throw "OTA start failed: HTTP $([int]$startResponse.StatusCode) $startText"
    }

    Write-Host "Gateway-to-H563 transfer queued at preferred baud $BaudRate."
    $deadline = (Get-Date).AddMinutes(10)
    do {
        Start-Sleep -Milliseconds 500
        $statusText = $client.GetStringAsync(
            "$baseUri/api/ota/h563/status"
        ).GetAwaiter().GetResult()
        $status = $statusText | ConvertFrom-Json
        $transfer = $status.transfer
        $percent = if ($transfer.total -gt 0) {
            [Math]::Floor(100.0 * $transfer.transferred / $transfer.total)
        } else { 0 }
        Write-Progress -Activity "RS485 H563 signed OTA" -Status "$($transfer.phase): $($transfer.transferred) / $($transfer.total) bytes, baud=$($transfer.active_baud)" -PercentComplete $percent
        if ($transfer.phase -eq "complete") {
            Write-Progress -Activity "RS485 H563 signed OTA" -Completed
            Write-Host "H563 OTA complete; target reset requested."
            break
        }
        if ($transfer.phase -eq "failed") {
            throw "H563 OTA failed: result=$($transfer.result), remote_status=$($transfer.remote_status)"
        }
    } while ((Get-Date) -lt $deadline)

    if ((Get-Date) -ge $deadline) {
        throw "Timed out waiting for H563 OTA completion"
    }
}
finally {
    $client.Dispose()
}
