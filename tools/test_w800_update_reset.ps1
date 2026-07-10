param(
  [uint32]$Version = 0,
  [string]$HttpHost = "192.168.1.4",
  [int]$HttpPort = 8088,
  [string]$BrokerHost = "127.0.0.1",
  [int]$BrokerPort = 1883,
  [uint32]$ResetAfterOffset = 131072,
  [int]$RecoveryTimeoutSeconds = 1200
)

<#
  test_w800_update_reset.ps1

  Purpose:
    Verify power-loss-style recovery of the external Flash A/B asset layout.
    The script resets the MCU through CMSIS-DAP after the inactive slot has
    received real image data, requires the board to boot from the previous
    valid active slot, then retries and commits the same candidate package.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_w800_update_reset.ps1"

  Constraints:
    Only a target reset is issued; internal Flash and the bootloader are not
    erased or programmed. Desktop Debug Assistant must remain running.
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build_ui_assets.ps1"
$VerifyScript = Join-Path $PSScriptRoot "verify_ui_asset_http_range.ps1"
$RequestScript = Join-Path $PSScriptRoot "request_ui_http_manifest_update.py"
$Python = Join-Path $PSScriptRoot "python-3.12.4-embed-amd64\python.exe"
$Pack = Join-Path $env:LOCALAPPDATA "Arm\Packs\.Download\Keil.STM32H5xx_DFP.1.2.0.pack"
$AssetFile = Join-Path $Root "build\ui_assets\ui_assets.bin"
$BrokerListener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
                  Select-Object -First 1
if($null -eq $BrokerListener) { throw "MQTT broker is not listening on port $BrokerPort." }
$BrokerProcess = Get-Process -Id $BrokerListener.OwningProcess -ErrorAction Stop
$EventLog = Join-Path (Split-Path -Parent $BrokerProcess.Path) "logs\latest-events.jsonl"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LogDir = Join-Path $PSScriptRoot "logs\w800_update_reset_$RunId"

function Get-RecentEvents {
  $events = New-Object System.Collections.Generic.List[object]
  foreach($line in (Get-Content -LiteralPath $EventLog -Tail 3000 -Encoding utf8)) {
    try { $events.Add(($line | ConvertFrom-Json)) } catch { }
  }
  return $events
}

function Get-LatestW800StatusEvent {
  param([DateTime]$NotBefore)
  $events = @(Get-RecentEvents)
  for($i = $events.Count - 1; $i -ge 0; $i--) {
    $event = $events[$i]
    if($event.event -ne "publish" -or $event.topic -ne "leduo/w800/status") { continue }
    try {
      if(([DateTime]::Parse([string]$event.at)).ToUniversalTime() -ge $NotBefore.ToUniversalTime()) {
        return [pscustomobject]@{ Event = $event; Status = ($event.payload | ConvertFrom-Json) }
      }
    } catch { }
  }
  return $null
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

function Wait-BoardRange {
  param([uint32]$CandidateVersion, [uint32]$MinimumStart, [DateTime]$NotBefore, [int]$WaitSeconds)
  $deadline = (Get-Date).AddSeconds($WaitSeconds)
  while((Get-Date) -lt $deadline) {
    $match = $null
    foreach($event in (Get-RecentEvents)) {
      if($event.event -ne "asset-http-range" -or
         [uint32]$event.version -ne $CandidateVersion -or
         [uint32]$event.start -lt $MinimumStart -or
         [string]$event.remote -match "127\.0\.0\.1|::1") { continue }
      try {
        if(([DateTime]::Parse([string]$event.at)).ToUniversalTime() -ge $NotBefore.ToUniversalTime()) {
          $match = $event
        }
      } catch { }
    }
    if($null -ne $match) { return $match }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for board range start >= $MinimumStart for version $CandidateVersion."
}

function Wait-StatusVersion {
  param([uint32]$ExpectedVersion, [DateTime]$NotBefore, [int]$WaitSeconds, [switch]$RequireDone)
  $deadline = (Get-Date).AddSeconds($WaitSeconds)
  $last = $null
  while((Get-Date) -lt $deadline) {
    $item = Get-LatestW800StatusEvent -NotBefore $NotBefore
    if($null -ne $item) {
      $last = $item.Status
      $versionMatches = [uint32]$last.asset.version -eq $ExpectedVersion
      $doneMatches = !$RequireDone -or
        ([string]$last.asset.error -eq "none" -and
         [int]$last.http.state -eq 3 -and
         [string]::IsNullOrWhiteSpace([string]$last.http.error))
      if($versionMatches -and $doneMatches) { return $last }
    }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for active version $ExpectedVersion. Last status: $($last | ConvertTo-Json -Compress -Depth 8)"
}

if(!(Test-Path -LiteralPath $Python)) { throw "Python runtime not found: $Python" }
if(!(Test-Path -LiteralPath $Pack)) { throw "CMSIS pack not found: $Pack" }

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$baseline = Get-LatestW800Status
$baselineVersion = [uint32]$baseline.asset.version
if($Version -eq 0) { $Version = [uint32]($baselineVersion + 1) }
if($Version -le $baselineVersion) { throw "Test version $Version must be newer than active version $baselineVersion." }

Write-Host "W800 update reset recovery test"
Write-Host "  Baseline : $baselineVersion"
Write-Host "  Candidate: $Version"
Write-Host "  Reset after HTTP offset: $ResetAfterOffset"
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
  Tee-Object -FilePath (Join-Path $LogDir "publish-before-reset.log")
if($LASTEXITCODE -ne 0) { throw "Update command publish failed with exit code $LASTEXITCODE." }

$rangeBeforeReset = Wait-BoardRange -CandidateVersion $Version -MinimumStart $ResetAfterOffset -NotBefore $started -WaitSeconds 240
$resetAt = Get-Date
& $Python -m pyocd reset `
  --target stm32h563rivx `
  --frequency 1000000 `
  --pack $Pack *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "target-reset.log")
if($LASTEXITCODE -ne 0) { throw "CMSIS-DAP target reset failed with exit code $LASTEXITCODE." }

$statusAfterReset = Wait-StatusVersion -ExpectedVersion $baselineVersion -NotBefore $resetAt -WaitSeconds 180
$recoveryStarted = Get-Date
& $Python $RequestScript `
  --broker-host $BrokerHost `
  --broker-port $BrokerPort `
  --http-host $HttpHost `
  --http-port $HttpPort `
  --asset $AssetFile `
  --timeout $RecoveryTimeoutSeconds *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "recovery-update.log")
if($LASTEXITCODE -ne 0) { throw "Recovery update failed with exit code $LASTEXITCODE." }

$finalStatus = Wait-StatusVersion -ExpectedVersion $Version -NotBefore $recoveryStarted -WaitSeconds 120 -RequireDone
$summary = [pscustomobject]@{
  test = "mcu-reset-during-inactive-slot-staging"
  baselineVersion = $baselineVersion
  candidateVersion = $Version
  resetAfterRangeStart = [uint32]$rangeBeforeReset.start
  activeVersionAfterReset = [uint32]$statusAfterReset.asset.version
  assetAvailableAfterReset = [int]$statusAfterReset.asset.available
  decoderReadFailuresAfterReset = [uint32]$statusAfterReset.asset.dec.readFail
  recoveryElapsedSeconds = [int]((Get-Date) - $recoveryStarted).TotalSeconds
  finalVersion = [uint32]$finalStatus.asset.version
  finalAssetError = [string]$finalStatus.asset.error
  finalHttpError = [string]$finalStatus.http.error
  result = "pass"
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $LogDir "summary.json") -Encoding utf8
$summary | Format-List
