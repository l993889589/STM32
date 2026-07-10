param(
  [string]$HttpHost = "192.168.1.4",
  [int]$HttpPort = 8088,
  [string]$BrokerHost = "127.0.0.1",
  [int]$BrokerPort = 1883
)

<#
  test_ui_asset_rollback_guard.ps1

  Purpose:
    Verify that an older UI asset manifest cannot erase or replace the current
    active A/B slot. The server package is temporarily rebuilt one version
    lower, the expected rejection is observed, and the server package is then
    restored to the active version without asking the board to redownload it.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\test_ui_asset_rollback_guard.ps1"
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build_ui_assets.ps1"
$RequestScript = Join-Path $PSScriptRoot "request_ui_http_manifest_update.py"
$Python = Join-Path $PSScriptRoot "python-3.12.4-embed-amd64\python.exe"
$AssetFile = Join-Path $Root "build\ui_assets\ui_assets.bin"
$listener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
            Select-Object -First 1
if($null -eq $listener) { throw "MQTT broker is not listening on port $BrokerPort." }
$owner = Get-Process -Id $listener.OwningProcess -ErrorAction Stop
$EventLog = Join-Path (Split-Path -Parent $owner.Path) "logs\latest-events.jsonl"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LogDir = Join-Path $PSScriptRoot "logs\ui_asset_rollback_$RunId"

function Get-LatestW800Status {
  $lines = @(Get-Content -LiteralPath $EventLog -Tail 2000 -Encoding utf8)
  for($i = $lines.Count - 1; $i -ge 0; $i--) {
    try {
      $event = $lines[$i] | ConvertFrom-Json
      if($event.event -eq "publish" -and $event.topic -eq "leduo/w800/status") {
        return $event.payload | ConvertFrom-Json
      }
    } catch { }
  }
  throw "No W800 status found in $EventLog"
}

function Build-AssetVersion {
  param([uint32]$Version, [string]$LogName)
  $oldVersion = $env:UI_ASSET_VERSION
  $env:UI_ASSET_VERSION = [string]$Version
  try {
    powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -ClearStaticValues *>&1 |
      Tee-Object -FilePath (Join-Path $LogDir $LogName)
    if($LASTEXITCODE -ne 0) { throw "Asset build failed with exit code $LASTEXITCODE." }
  } finally {
    if($null -eq $oldVersion) { Remove-Item Env:\UI_ASSET_VERSION -ErrorAction SilentlyContinue }
    else { $env:UI_ASSET_VERSION = $oldVersion }
  }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$baseline = Get-LatestW800Status
$activeVersion = [uint32]$baseline.asset.version
if($activeVersion -le 1) { throw "Active version $activeVersion cannot be used for rollback testing." }
$olderVersion = [uint32]($activeVersion - 1)

Write-Host "UI asset rollback guard test"
Write-Host "  Active: $activeVersion"
Write-Host "  Offered older version: $olderVersion"

$statusAfterReject = $null
try {
  Build-AssetVersion -Version $olderVersion -LogName "build-older.log"
  $stdout = Join-Path $LogDir "request.stdout.log"
  $stderr = Join-Path $LogDir "request.stderr.log"
  $process = Start-Process -FilePath $Python `
    -ArgumentList @(
      $RequestScript,
      "--broker-host", $BrokerHost,
      "--broker-port", [string]$BrokerPort,
      "--http-host", $HttpHost,
      "--http-port", [string]$HttpPort,
      "--asset", $AssetFile,
      "--timeout", "120"
    ) `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue
  Get-Content -LiteralPath $stderr -ErrorAction SilentlyContinue
  if($process.ExitCode -eq 0) { throw "Older version $olderVersion was unexpectedly accepted." }

  $deadline = (Get-Date).AddSeconds(30)
  while((Get-Date) -lt $deadline) {
    $statusAfterReject = Get-LatestW800Status
    if([uint32]$statusAfterReject.asset.version -eq $activeVersion -and
       [string]$statusAfterReject.asset.error -eq "old version") { break }
    Start-Sleep -Seconds 2
  }
  if($null -eq $statusAfterReject -or
     [uint32]$statusAfterReject.asset.version -ne $activeVersion -or
     [string]$statusAfterReject.asset.error -ne "old version") {
    throw "Rollback rejection status was not observed."
  }
} finally {
  Build-AssetVersion -Version $activeVersion -LogName "build-restore.log"
}

$summary = [pscustomobject]@{
  test = "ui-asset-rollback-guard"
  activeVersion = $activeVersion
  rejectedVersion = $olderVersion
  activeSlot = [uint32]$statusAfterReject.asset.slot
  assetError = [string]$statusAfterReject.asset.error
  httpError = [string]$statusAfterReject.http.error
  serverPackageRestoredVersion = $activeVersion
  result = "pass"
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $LogDir "summary.json") -Encoding utf8
$summary | Format-List
