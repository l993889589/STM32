param(
  [int]$SampleCount = 4,
  [int]$SampleIntervalSeconds = 22,
  [int]$BrokerPort = 1883
)

<#
  test_ui_asset_runtime.ps1

  Purpose:
    Verify that LVGL keeps decoding external-Flash pages after an update. The
    default observation window spans more than three 20-second automatic page
    periods and requires decoder activity to increase without read failures.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_ui_asset_runtime.ps1"

  Constraints:
    Uses MQTT status logs only; it does not change the device or asset package.
#>

$ErrorActionPreference = "Stop"

if($SampleCount -lt 2 -or $SampleIntervalSeconds -lt 5) {
  throw "SampleCount must be >= 2 and SampleIntervalSeconds must be >= 5."
}

$listener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
            Select-Object -First 1
if($null -eq $listener) { throw "MQTT broker is not listening on port $BrokerPort." }
$owner = Get-Process -Id $listener.OwningProcess -ErrorAction Stop
$eventLog = Join-Path (Split-Path -Parent $owner.Path) "logs\latest-events.jsonl"
$runId = Get-Date -Format "yyyyMMdd_HHmmss"
$logDir = Join-Path $PSScriptRoot "logs\ui_asset_runtime_$runId"
$summaryPath = Join-Path $logDir "summary.json"

function Get-LatestW800Status {
  $lines = @(Get-Content -LiteralPath $eventLog -Tail 2000 -Encoding utf8)
  for($i = $lines.Count - 1; $i -ge 0; $i--) {
    try {
      $event = $lines[$i] | ConvertFrom-Json
      if($event.event -eq "publish" -and $event.topic -eq "leduo/w800/status") {
        return $event.payload | ConvertFrom-Json
      }
    } catch { }
  }
  throw "No W800 status found in $eventLog"
}

New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$samples = New-Object System.Collections.Generic.List[object]
for($i = 0; $i -lt $SampleCount; $i++) {
  $status = Get-LatestW800Status
  $samples.Add([pscustomobject]@{
    sample = $i + 1
    at = (Get-Date).ToString("o")
    version = [uint32]$status.asset.version
    slot = [uint32]$status.asset.slot
    info = [uint32]$status.asset.dec.info
    open = [uint32]$status.asset.dec.open
    area = [uint32]$status.asset.dec.area
    readFail = [uint32]$status.asset.dec.readFail
  })
  if($i -lt ($SampleCount - 1)) { Start-Sleep -Seconds $SampleIntervalSeconds }
}

$first = $samples[0]
$last = $samples[$samples.Count - 1]
$observationSeconds = ($SampleCount - 1) * $SampleIntervalSeconds
$passed = $first.version -eq $last.version -and
          $last.version -ne 0 -and
          $first.slot -eq $last.slot -and
          $last.info -gt $first.info -and
          $last.open -gt $first.open -and
          $last.area -gt $first.area -and
          $last.readFail -eq 0

$summary = [pscustomobject]@{
  test = "lvgl-external-flash-runtime"
  observationSeconds = $observationSeconds
  autoPagePeriodMs = 20000
  version = $last.version
  slot = $last.slot
  decoderInfoDelta = $last.info - $first.info
  decoderOpenDelta = $last.open - $first.open
  decoderAreaDelta = $last.area - $first.area
  decoderReadFailures = $last.readFail
  samples = $samples
  result = $(if($passed) { "pass" } else { "fail" })
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | Select-Object test,observationSeconds,version,slot,decoderInfoDelta,decoderOpenDelta,decoderAreaDelta,decoderReadFailures,result | Format-List

if(!$passed) { throw "LVGL external-Flash runtime acceptance failed. See $summaryPath" }
