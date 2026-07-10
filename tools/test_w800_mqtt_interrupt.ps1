param(
  [uint32]$Version = 0,
  [string]$HttpHost = "192.168.1.4",
  [int]$HttpPort = 8088,
  [string]$BrokerHost = "127.0.0.1",
  [int]$BrokerPort = 1883,
  [uint32]$DisconnectAfterOffset = 65536,
  [int]$TimeoutSeconds = 1200
)

<#
  test_w800_mqtt_interrupt.ps1

  Purpose:
    Verify that MQTT is only the control/status plane. The script starts a real
    HTTP Range asset update, stops only the Desktop Debug Assistant MQTT broker
    after HTTP staging has progressed, waits for the final HTTP range while the
    broker remains down, restarts MQTT, and requires the board to reconnect and
    report the committed candidate version.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_w800_mqtt_interrupt.ps1"

  Constraints:
    Requires the release containing the loopback-only /__test/mqtt-broker
    endpoint. The HTTP server remains online for the full test.
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build_ui_assets.ps1"
$VerifyScript = Join-Path $PSScriptRoot "verify_ui_asset_http_range.ps1"
$RequestScript = Join-Path $PSScriptRoot "request_ui_http_manifest_update.py"
$Python = Join-Path $PSScriptRoot "python-3.12.4-embed-amd64\python.exe"
$AssetFile = Join-Path $Root "build\ui_assets\ui_assets.bin"
$BrokerControl = "http://127.0.0.1:$HttpPort/__test/mqtt-broker"
$BrokerListener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
                  Select-Object -First 1
if($null -eq $BrokerListener) {
  throw "MQTT broker is not listening on port $BrokerPort."
}
$BrokerProcess = Get-Process -Id $BrokerListener.OwningProcess -ErrorAction Stop
$EventLog = Join-Path (Split-Path -Parent $BrokerProcess.Path) "logs\latest-events.jsonl"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LogDir = Join-Path $PSScriptRoot "logs\w800_mqtt_interrupt_$RunId"

function Get-RecentEvents {
  $events = New-Object System.Collections.Generic.List[object]
  foreach($line in (Get-Content -LiteralPath $EventLog -Tail 3000 -Encoding utf8)) {
    try { $events.Add(($line | ConvertFrom-Json)) } catch { }
  }
  return $events
}

function Get-LatestW800Status {
  $events = @(Get-RecentEvents)
  for($i = $events.Count - 1; $i -ge 0; $i--) {
    if($events[$i].event -eq "publish" -and $events[$i].topic -eq "leduo/w800/status") {
      return $events[$i].payload | ConvertFrom-Json
    }
  }
  throw "No W800 status found in $EventLog"
}

function Wait-RangeOffset {
  param(
    [uint32]$CandidateVersion,
    [uint32]$MinimumStart,
    [DateTime]$NotBefore,
    [int]$WaitSeconds
  )
  $deadline = (Get-Date).AddSeconds($WaitSeconds)
  while((Get-Date) -lt $deadline) {
    $ranges = @()
    foreach($event in (Get-RecentEvents)) {
      if($event.event -ne "asset-http-range" -or
         [uint32]$event.version -ne $CandidateVersion -or
         [uint32]$event.start -lt $MinimumStart -or
         [string]$event.remote -match "127\.0\.0\.1|::1") {
        continue
      }
      try {
        if(([DateTime]::Parse([string]$event.at)).ToUniversalTime() -ge $NotBefore.ToUniversalTime()) {
          $ranges += $event
        }
      } catch { }
    }
    if($ranges.Count -gt 0) {
      return $ranges[-1]
    }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for version $CandidateVersion range start >= $MinimumStart."
}

function Wait-W800Version {
  param([uint32]$CandidateVersion, [int]$WaitSeconds)
  $deadline = (Get-Date).AddSeconds($WaitSeconds)
  $last = $null
  while((Get-Date) -lt $deadline) {
    $last = Get-LatestW800Status
    if([uint32]$last.asset.version -eq $CandidateVersion -and
       [string]$last.asset.error -eq "none" -and
       [int]$last.http.state -eq 3 -and
       [string]::IsNullOrWhiteSpace([string]$last.http.error)) {
      return $last
    }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for committed version $CandidateVersion. Last status: $($last | ConvertTo-Json -Compress -Depth 8)"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$baseline = Get-LatestW800Status
$baselineVersion = [uint32]$baseline.asset.version
if($Version -eq 0) {
  $Version = [uint32]($baselineVersion + 1)
}
if($Version -le $baselineVersion) {
  throw "Test version $Version must be newer than active version $baselineVersion."
}

Write-Host "W800 MQTT interruption test"
Write-Host "  Baseline : $baselineVersion"
Write-Host "  Candidate: $Version"
Write-Host "  Disconnect MQTT after HTTP offset: $DisconnectAfterOffset"
Write-Host "  Logs     : $LogDir"

$oldVersion = $env:UI_ASSET_VERSION
$env:UI_ASSET_VERSION = [string]$Version
try {
  powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -ClearStaticValues *>&1 |
    Tee-Object -FilePath (Join-Path $LogDir "build.log")
  if($LASTEXITCODE -ne 0) { throw "Asset build failed with exit code $LASTEXITCODE." }
} finally {
  if($null -eq $oldVersion) { Remove-Item Env:\UI_ASSET_VERSION -ErrorAction SilentlyContinue }
  else { $env:UI_ASSET_VERSION = $oldVersion }
}

powershell -NoProfile -ExecutionPolicy Bypass -File $VerifyScript `
  -BaseUrl "http://127.0.0.1:$HttpPort" -AssetFile $AssetFile *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "verify.log")
if($LASTEXITCODE -ne 0) { throw "Host HTTP Range verification failed with exit code $LASTEXITCODE." }

$started = Get-Date
& $Python $RequestScript `
  --broker-host $BrokerHost `
  --broker-port $BrokerPort `
  --http-host $HttpHost `
  --http-port $HttpPort `
  --asset $AssetFile `
  --no-wait *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "publish.log")
if($LASTEXITCODE -ne 0) { throw "Update command publish failed with exit code $LASTEXITCODE." }

$disconnectRange = Wait-RangeOffset -CandidateVersion $Version -MinimumStart $DisconnectAfterOffset -NotBefore $started -WaitSeconds 180
$mqttStoppedAt = Get-Date
$mqttRestarted = $false
try {
  Invoke-RestMethod -Uri "$BrokerControl`?action=stop" -Method Post |
    ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $LogDir "mqtt-stop.json") -Encoding utf8

  $finalRangeStart = [uint32]((Get-Item -LiteralPath $AssetFile).Length - 4096)
  $finalRange = Wait-RangeOffset -CandidateVersion $Version -MinimumStart $finalRangeStart -NotBefore $started -WaitSeconds $TimeoutSeconds

  Invoke-RestMethod -Uri "$BrokerControl`?action=start" -Method Post |
    ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $LogDir "mqtt-start.json") -Encoding utf8
  $mqttRestarted = $true
} finally {
  if(!$mqttRestarted) {
    Invoke-RestMethod -Uri "$BrokerControl`?action=start" -Method Post -ErrorAction SilentlyContinue | Out-Null
  }
}

$finalStatus = Wait-W800Version -CandidateVersion $Version -WaitSeconds 120
$summary = [pscustomobject]@{
  test = "mqtt-control-plane-interruption"
  baselineVersion = $baselineVersion
  candidateVersion = $Version
  mqttStoppedAfterRangeStart = [uint32]$disconnectRange.start
  mqttDownSeconds = [int]((Get-Date) - $mqttStoppedAt).TotalSeconds
  finalRangeStart = [uint32]$finalRange.start
  finalRangeEnd = [uint32]$finalRange.end
  elapsedSeconds = [int]((Get-Date) - $started).TotalSeconds
  finalVersion = [uint32]$finalStatus.asset.version
  finalAssetError = [string]$finalStatus.asset.error
  finalHttpError = [string]$finalStatus.http.error
  result = "pass"
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $LogDir "summary.json") -Encoding utf8
$summary | Format-List
