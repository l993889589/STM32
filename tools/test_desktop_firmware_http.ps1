<#
.SYNOPSIS
Validates the desktop assistant signed firmware manifest, Range CRC, and retry path.

.DESCRIPTION
The test uses the loopback-only fault endpoint. It injects one HTTP 503 response at
offset 4096, proves the first request fails, then proves the identical retry returns
the original bytes and a matching X-Range-CRC32 header.
#>
param(
    [string]$BaseUrl = "http://127.0.0.1:8088"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

function Get-Crc32([byte[]]$Bytes) {
    [uint32]$crc = [uint32]::MaxValue
    [uint32]$polynomial = [Convert]::ToUInt32("EDB88320", 16)
    foreach($value in $Bytes) {
        $crc = $crc -bxor [uint32]$value
        for($bit = 0; $bit -lt 8; $bit++) {
            if(($crc -band 1) -ne 0) {
                $crc = ($crc -shr 1) -bxor $polynomial
            } else {
                $crc = $crc -shr 1
            }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Send-Request(
    [System.Net.Http.HttpClient]$Client,
    [System.Net.Http.HttpMethod]$Method,
    [string]$Url,
    [string]$Range = ""
) {
    if(-not [Uri]::IsWellFormedUriString($Url, [UriKind]::Absolute)) {
        throw "Invalid absolute HTTP test URL: '$Url'"
    }
    $request = [System.Net.Http.HttpRequestMessage]::new($Method, [Uri]$Url)
    try {
        if(-not [string]::IsNullOrWhiteSpace($Range)) {
            $request.Headers.TryAddWithoutValidation("Range", $Range) | Out-Null
        }
        return $Client.SendAsync($request).GetAwaiter().GetResult()
    }
    finally {
        $request.Dispose()
    }
}

$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(15)
$faultUrl = "$BaseUrl/__test/http-fault"
try {
    $manifestResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Get) "$BaseUrl/firmware/manifest.json"
    try {
        if([int]$manifestResponse.StatusCode -ne 200) {
            throw "Firmware manifest returned HTTP $([int]$manifestResponse.StatusCode)"
        }
        $manifest = $manifestResponse.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
    }
    finally {
        $manifestResponse.Dispose()
    }

    if([uint32]$manifest.imageFlags -ne 2) { throw "Manifest is not marked signed" }
    if(([string]$manifest.sha256).Length -ne 64) { throw "Manifest SHA-256 is missing" }
    if(([string]$manifest.signature).Length -ne 128) { throw "Manifest ECDSA signature is missing" }
    if([uint32]$manifest.chunkSize -ne 4096) { throw "Expected 4096-byte firmware chunks" }

    $rangeResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Get) "$BaseUrl/firmware/app.bin" "bytes=0-4095"
    try {
        if([int]$rangeResponse.StatusCode -ne 206) { throw "Initial Range returned HTTP $([int]$rangeResponse.StatusCode)" }
        $rangeBytes = $rangeResponse.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
        if($rangeBytes.Length -ne 4096) { throw "Initial Range length is $($rangeBytes.Length), expected 4096" }
        $serverCrc = [uint32]::Parse(($rangeResponse.Headers.GetValues("X-Range-CRC32") | Select-Object -First 1))
        $localCrc = Get-Crc32 $rangeBytes
        if($serverCrc -ne $localCrc) { throw "Initial Range CRC mismatch" }
    }
    finally {
        $rangeResponse.Dispose()
    }

    $armResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Post) "${faultUrl}?mode=status&afterOffset=4096&count=1"
    try {
        if([int]$armResponse.StatusCode -ne 200) { throw "Fault arm returned HTTP $([int]$armResponse.StatusCode)" }
    }
    finally {
        $armResponse.Dispose()
    }

    $faultResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Get) "$BaseUrl/firmware/app.bin" "bytes=4096-8191"
    try {
        if([int]$faultResponse.StatusCode -ne 503) {
            throw "Injected Range returned HTTP $([int]$faultResponse.StatusCode), expected 503"
        }
    }
    finally {
        $faultResponse.Dispose()
    }

    $retryResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Get) "$BaseUrl/firmware/app.bin" "bytes=4096-8191"
    try {
        if([int]$retryResponse.StatusCode -ne 206) { throw "Range retry returned HTTP $([int]$retryResponse.StatusCode)" }
        $retryBytes = $retryResponse.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
        if($retryBytes.Length -ne 4096) { throw "Range retry length is $($retryBytes.Length), expected 4096" }
        $serverCrc = [uint32]::Parse(($retryResponse.Headers.GetValues("X-Range-CRC32") | Select-Object -First 1))
        $localCrc = Get-Crc32 $retryBytes
        if($serverCrc -ne $localCrc) { throw "Range retry CRC mismatch" }
    }
    finally {
        $retryResponse.Dispose()
    }

    Write-Host ("Desktop firmware HTTP test passed: version={0}, size={1}, chunk=4096" -f $manifest.version, $manifest.size)
}
finally {
    try {
        $clearResponse = Send-Request $client ([System.Net.Http.HttpMethod]::Delete) $faultUrl
        $clearResponse.Dispose()
    } catch {
        Write-Warning "Could not clear HTTP fault hook: $($_.Exception.Message)"
    }
    $client.Dispose()
}
