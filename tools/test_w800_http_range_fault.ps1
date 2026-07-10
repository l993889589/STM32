param(
  [uint32]$Version = 0,
  [string]$HttpHost = "192.168.1.4",
  [int]$HttpPort = 8088,
  [string]$BrokerHost = "127.0.0.1",
  [int]$BrokerPort = 1883,
  [uint32]$FaultAfterOffset = 65536,
  [int]$FaultCount = 16,
  [int]$FailureTimeoutSeconds = 360,
  [int]$RecoveryTimeoutSeconds = 1200
)

<#
  test_w800_http_range_fault.ps1

  Purpose:
    Prove the UI asset A/B safety contract on real hardware. The script builds
    a new masked package, injects repeated HTTP Range connection failures after
    staging has started, verifies that the old active version remains selected,
    clears the fault, and verifies that the same package can then commit.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_w800_http_range_fault.ps1"

  Constraints:
    Desktop Debug Assistant must be the current release with the loopback-only
    /__test/ui-http-fault endpoint. MQTT remains command/status only; asset bytes
    are transferred solely through HTTP Range.
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build_ui_assets.ps1"
$VerifyScript = Join-Path $PSScriptRoot "verify_ui_asset_http_range.ps1"
$RequestScript = Join-Path $PSScriptRoot "request_ui_http_manifest_update.py"
$Python = Join-Path $PSScriptRoot "python-3.12.4-embed-amd64\python.exe"
$AssetFile = Join-Path $Root "build\ui_assets\ui_assets.bin"
$BrokerListener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
                  Select-Object -First 1
if($null -eq $BrokerListener) {
  throw "MQTT broker is not listening on port $BrokerPort."
}
$BrokerProcess = Get-Process -Id $BrokerListener.OwningProcess -ErrorAction Stop
$EventLog = Join-Path (Split-Path -Parent $BrokerProcess.Path) "logs\latest-events.jsonl"
$FaultEndpoint = "http://127.0.0.1:$HttpPort/__test/ui-http-fault"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LogDir = Join-Path $PSScriptRoot "logs\w800_http_range_fault_$RunId"

function Get-LatestW800Status {
  if(!(Test-Path -LiteralPath $EventLog)) {
    throw "Desktop Debug Assistant event log not found: $EventLog"
  }

  $lines = @(Get-Content -LiteralPath $EventLog -Tail 2000 -Encoding utf8)
  for($i = $lines.Count - 1; $i -ge 0; $i--) {
    try {
      $event = $lines[$i] | ConvertFrom-Json
      if($event.event -eq "publish" -and $event.topic -eq "leduo/w800/status") {
        return $event.payload | ConvertFrom-Json
      }
    } catch {
      continue
    }
  }
  throw "No W800 status found in $EventLog"
}

function Wait-W800Status {
  param(
    [scriptblock]$Predicate,
    [int]$TimeoutSeconds,
    [string]$Description
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $last = $null
  while((Get-Date) -lt $deadline) {
    $last = Get-LatestW800Status
    if(& $Predicate $last) {
      return $last
    }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for $Description. Last status: $($last | ConvertTo-Json -Compress -Depth 8)"
}

if(!(Test-Path -LiteralPath $Python)) {
  throw "Python runtime not found: $Python"
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

Write-Host "W800 HTTP Range A/B fault test"
Write-Host "  Baseline : $baselineVersion"
Write-Host "  Candidate: $Version"
Write-Host "  Fault    : after=$FaultAfterOffset count=$FaultCount mode=close"
Write-Host "  Logs     : $LogDir"

$oldVersion = $env:UI_ASSET_VERSION
$env:UI_ASSET_VERSION = [string]$Version
try {
  powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -ClearStaticValues *>&1 |
    Tee-Object -FilePath (Join-Path $LogDir "build.log")
  if($LASTEXITCODE -ne 0) {
    throw "Asset build failed with exit code $LASTEXITCODE."
  }
} finally {
  if($null -eq $oldVersion) {
    Remove-Item Env:\UI_ASSET_VERSION -ErrorAction SilentlyContinue
  } else {
    $env:UI_ASSET_VERSION = $oldVersion
  }
}

powershell -NoProfile -ExecutionPolicy Bypass -File $VerifyScript `
  -BaseUrl "http://127.0.0.1:$HttpPort" `
  -AssetFile $AssetFile *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "verify.log")
if($LASTEXITCODE -ne 0) {
  throw "Host HTTP Range verification failed with exit code $LASTEXITCODE."
}

$failureStarted = Get-Date
$failureElapsedSeconds = 0
$failureStatus = $null
$faultSnapshot = $null
try {
  Invoke-RestMethod -Uri "$FaultEndpoint`?mode=close&afterOffset=$FaultAfterOffset&count=$FaultCount" -Method Post |
    ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $LogDir "fault-arm.json") -Encoding utf8

  $faultStdout = Join-Path $LogDir "fault-update.stdout.log"
  $faultStderr = Join-Path $LogDir "fault-update.stderr.log"
  $failureProcess = Start-Process -FilePath $Python `
    -ArgumentList @(
      $RequestScript,
      "--broker-host", $BrokerHost,
      "--broker-port", [string]$BrokerPort,
      "--http-host", $HttpHost,
      "--http-port", [string]$HttpPort,
      "--asset", $AssetFile,
      "--timeout", [string]$FailureTimeoutSeconds
    ) `
    -NoNewWindow `
    -Wait `
    -PassThru `
    -RedirectStandardOutput $faultStdout `
    -RedirectStandardError $faultStderr
  Get-Content -LiteralPath $faultStdout -ErrorAction SilentlyContinue
  Get-Content -LiteralPath $faultStderr -ErrorAction SilentlyContinue
  $failureExit = $failureProcess.ExitCode
  if($failureExit -eq 0) {
    throw "Faulted update unexpectedly committed version $Version."
  }

  $failureStatus = Wait-W800Status -TimeoutSeconds 60 -Description "faulted update result" -Predicate {
    param($status)
    [uint32]$status.asset.version -eq $baselineVersion -and
      ![string]::IsNullOrWhiteSpace([string]$status.http.error)
  }
  $faultSnapshot = Invoke-RestMethod -Uri $FaultEndpoint -Method Get
  if([int]$faultSnapshot.fault.hits -le 0) {
    throw "Desktop Debug Assistant did not record a fault hit."
  }
  $failureElapsedSeconds = [int]((Get-Date) - $failureStarted).TotalSeconds
} finally {
  Invoke-RestMethod -Uri $FaultEndpoint -Method Delete -ErrorAction SilentlyContinue | Out-Null
}

$recoveryStarted = Get-Date
& $Python $RequestScript `
  --broker-host $BrokerHost `
  --broker-port $BrokerPort `
  --http-host $HttpHost `
  --http-port $HttpPort `
  --asset $AssetFile `
  --timeout $RecoveryTimeoutSeconds *>&1 |
  Tee-Object -FilePath (Join-Path $LogDir "recovery-update.log")
if($LASTEXITCODE -ne 0) {
  throw "Recovery update failed with exit code $LASTEXITCODE."
}

$finalStatus = Wait-W800Status -TimeoutSeconds 60 -Description "candidate version commit" -Predicate {
  param($status)
  [uint32]$status.asset.version -eq $Version -and
    [string]$status.asset.error -eq "none" -and
    [int]$status.http.state -eq 3 -and
    [string]::IsNullOrWhiteSpace([string]$status.http.error)
}

$summary = [pscustomobject]@{
  test = "http-range-ab-fault-recovery"
  baselineVersion = $baselineVersion
  candidateVersion = $Version
  failureElapsedSeconds = $failureElapsedSeconds
  faultHits = [int]$faultSnapshot.fault.hits
  failureHttpError = [string]$failureStatus.http.error
  activeVersionAfterFailure = [uint32]$failureStatus.asset.version
  recoveryElapsedSeconds = [int]((Get-Date) - $recoveryStarted).TotalSeconds
  finalVersion = [uint32]$finalStatus.asset.version
  finalAssetError = [string]$finalStatus.asset.error
  finalHttpError = [string]$finalStatus.http.error
  result = "pass"
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $LogDir "summary.json") -Encoding utf8
$summary | Format-List
