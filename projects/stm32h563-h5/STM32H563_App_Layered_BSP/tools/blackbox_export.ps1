<#
.SYNOPSIS
Exports STM32H563 SPI-NOR black-box records over the USB CDC shell.

.DESCRIPTION
Requests machine-readable records, validates the export markers, and writes
JSON, CSV, and a standalone HTML summary without external PowerShell modules.
#>

[CmdletBinding()]
param(
    [string]$port = 'COM4',
    [ValidateRange(1, 32)]
    [int]$count = 32,
    [string]$output_directory = ''
)

$ErrorActionPreference = 'Stop'

<# Opens the CDC port with the line settings used by the application shell. #>
function open_blackbox_port {
    param([string]$port_name)

    $serial = [System.IO.Ports.SerialPort]::new(
        $port_name,
        115200,
        [System.IO.Ports.Parity]::None,
        8,
        [System.IO.Ports.StopBits]::One)
    $serial.DtrEnable = $true
    $serial.ReadTimeout = 500
    $serial.WriteTimeout = 1500
    $serial.NewLine = "`r`n"
    $serial.Open()
    return $serial
}

<# Reads one marker-bounded NDJSON export with a fixed overall timeout. #>
function read_blackbox_records {
    param(
        [System.IO.Ports.SerialPort]$serial,
        [int]$requested_count
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    $records = [System.Collections.Generic.List[object]]::new()
    $begin_seen = $false
    $end_seen = $false

    $serial.DiscardInBuffer()
    $serial.Write("blackbox export $requested_count`r`n")
    while([DateTime]::UtcNow -lt $deadline -and -not $end_seen) {
        try {
            $line = $serial.ReadLine().Trim()
        }
        catch [System.TimeoutException] {
            continue
        }
        if($line -like 'blackbox_export_begin*') {
            $begin_seen = $true
            continue
        }
        if($line -like 'blackbox_export_end*') {
            $end_seen = $true
            continue
        }
        if($begin_seen -and $line.StartsWith('{')) {
            $record = $line | ConvertFrom-Json
            if($record.kind -ne 'blackbox') {
                throw "Unexpected export kind '$($record.kind)'."
            }
            $records.Add($record)
        }
    }
    if(-not $begin_seen -or -not $end_seen) {
        throw 'Black-box export did not complete before the 12-second timeout.'
    }
    return $records.ToArray()
}

<# Converts numeric firmware fields to short operator-facing labels. #>
function get_blackbox_label {
    param([string]$category, [int]$value)

    if($category -eq 'severity') {
        return @('error', 'warning', 'info')[$value]
    }
    if($category -eq 'type') {
        $names = @{
            1 = 'boot'; 2 = 'log'; 3 = 'fault'; 4 = 'self_test'
            5 = 'power'; 6 = 'manual'
        }
        if($names.ContainsKey($value)) { return $names[$value] }
    }
    return "unknown_$value"
}

<# Writes JSON, CSV, and a self-contained HTML report for the exported data. #>
function write_blackbox_report {
    param(
        [object[]]$records,
        [string]$directory
    )

    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $json_path = Join-Path $directory 'blackbox.json'
    $csv_path = Join-Path $directory 'blackbox.csv'
    $html_path = Join-Path $directory 'blackbox_report.html'
    $normalized = foreach($record in $records) {
        [pscustomobject]@{
            sequence = [uint32]$record.sequence
            timestamp = [string]$record.timestamp
            rtc_valid = [bool]$record.rtc_valid
            type = get_blackbox_label 'type' ([int]$record.type)
            type_id = [int]$record.type
            severity = get_blackbox_label 'severity' ([int]$record.severity)
            severity_id = [int]$record.severity
            source = [int]$record.source
            code = [int]$record.code
            uptime_ms = [uint32]$record.uptime_ms
            flags = [int]$record.flags
            payload_length = [int]$record.payload_length
            payload_hex = [string]$record.payload_hex
        }
    }

    $normalized | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $json_path -Encoding utf8
    $normalized | Export-Csv -LiteralPath $csv_path -NoTypeInformation -Encoding utf8

    $error_count = @($normalized | Where-Object severity -eq 'error').Count
    $warning_count = @($normalized | Where-Object severity -eq 'warning').Count
    $rows = foreach($record in $normalized) {
        $color = if($record.severity -eq 'error') { '#ffdddd' }
                 elseif($record.severity -eq 'warning') { '#fff3cd' }
                 else { '#eef7ee' }
        "<tr style='background:$color'><td>$($record.sequence)</td><td>$($record.timestamp)</td><td>$($record.type)</td><td>$($record.severity)</td><td>$($record.source)</td><td>$($record.code)</td><td>$($record.uptime_ms)</td><td><code>$($record.payload_hex)</code></td></tr>"
    }
    $html = @"
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<title>STM32H563 黑匣子报告</title>
<style>body{font-family:Segoe UI,Microsoft YaHei,sans-serif;margin:24px;color:#222}table{border-collapse:collapse;width:100%}th,td{border:1px solid #bbb;padding:6px;text-align:left}th{background:#263238;color:white}.summary{display:flex;gap:18px;margin:16px 0}.card{padding:12px 18px;border:1px solid #bbb;border-radius:8px}</style>
</head><body><h1>STM32H563 黑匣子报告</h1>
<p>导出时间：$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))；端口：$port</p>
<div class="summary"><div class="card">记录：$($normalized.Count)</div><div class="card">错误：$error_count</div><div class="card">警告：$warning_count</div></div>
<table><thead><tr><th>序号</th><th>RTC</th><th>类型</th><th>级别</th><th>来源</th><th>代码</th><th>运行时间(ms)</th><th>载荷(hex)</th></tr></thead><tbody>
$($rows -join "`n")
</tbody></table></body></html>
"@
    Set-Content -LiteralPath $html_path -Value $html -Encoding utf8
    return [pscustomobject]@{
        record_count = $normalized.Count
        error_count = $error_count
        warning_count = $warning_count
        json_path = $json_path
        csv_path = $csv_path
        html_path = $html_path
    }
}

if([string]::IsNullOrWhiteSpace($output_directory)) {
    $stamp = [DateTime]::Now.ToString('yyyyMMdd_HHmmss')
    $output_directory = Join-Path $PSScriptRoot "artifacts\blackbox_$stamp"
}

$serial_port = $null
try {
    $serial_port = open_blackbox_port $port
    Start-Sleep -Milliseconds 500
    $exported_records = read_blackbox_records $serial_port $count
}
finally {
    if($null -ne $serial_port -and $serial_port.IsOpen) {
        $serial_port.Close()
    }
}

write_blackbox_report $exported_records $output_directory
