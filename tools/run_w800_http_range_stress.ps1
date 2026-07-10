param(
  [int]$Count = 10,
  [uint32]$StartVersion = 0,
  [string]$HttpHost = "192.168.1.4",
  [int]$HttpPort = 8088,
  [string]$BrokerHost = "127.0.0.1",
  [int]$BrokerPort = 1883,
  [int]$TimeoutSeconds = 2400,
  [int]$DelaySeconds = 5,
  [switch]$FullHttpVerify,
  [switch]$ClearStaticValues,
  [switch]$KeepStaticValues
)

<#
  run_w800_http_range_stress.ps1

  Purpose:
    Drive repeated W800 UI asset HTTP Range updates for acceptance testing.
    Each iteration rebuilds the UI asset package with a new version, verifies
    the PC HTTP Range service, publishes the MQTT control command, and waits
    for the board to report the committed version.

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\run_w800_http_range_stress.ps1" -Count 10
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\run_w800_http_range_stress.ps1" -Count 3 -FullHttpVerify
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\run_w800_http_range_stress.ps1" -Count 3 -KeepStaticValues

  Constraints:
    Desktop Debug Assistant must already be running with its HTTP asset server
    on HttpPort and MQTT broker on BrokerPort. The image data plane remains
    HTTP Range only; MQTT carries the manifest update command and status.
    Dynamic value zones are masked by default to avoid drawing live labels over
    static numbers baked into the reference pictures.
#>

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot "build_ui_assets.ps1"
$VerifyScript = Join-Path $PSScriptRoot "verify_ui_asset_http_range.ps1"
$RequestScript = Join-Path $PSScriptRoot "request_ui_http_manifest_update.py"
$AssetFile = Join-Path $Root "build\ui_assets\ui_assets.bin"
$LocalPython = Join-Path $PSScriptRoot "python-3.12.4-embed-amd64\python.exe"
$RunId = Get-Date -Format "yyyyMMdd_HHmmss"
$LogDir = Join-Path $PSScriptRoot "logs\w800_http_range_stress_$RunId"
$Summary = Join-Path $LogDir "summary.jsonl"

function Get-PythonCommand {
  if(Test-Path -LiteralPath $LocalPython) {
    return $LocalPython
  }
  $python = Get-Command python -ErrorAction SilentlyContinue
  if($python) {
    return $python.Source
  }
  throw "Python not found. Expected $LocalPython or python on PATH."
}

function Get-ActiveAssetVersion {
  $listener = Get-NetTCPConnection -LocalPort $BrokerPort -State Listen -ErrorAction SilentlyContinue |
              Select-Object -First 1
  if($null -ne $listener) {
    $owner = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
    if($null -ne $owner -and $owner.Path) {
      $eventLog = Join-Path (Split-Path -Parent $owner.Path) "logs\latest-events.jsonl"
      if(Test-Path -LiteralPath $eventLog) {
        $lines = @(Get-Content -LiteralPath $eventLog -Tail 2000 -Encoding utf8)
        for($i = $lines.Count - 1; $i -ge 0; $i--) {
          try {
            $event = $lines[$i] | ConvertFrom-Json
            if($event.event -eq "publish" -and $event.topic -eq "leduo/w800/status") {
              $status = $event.payload | ConvertFrom-Json
              return [uint32]$status.asset.version
            }
          } catch { }
        }
      }
    }
  }

  if(Test-Path -LiteralPath $AssetFile) {
    $header = [System.IO.File]::ReadAllBytes($AssetFile)
    if($header.Length -ge 28) {
      return [BitConverter]::ToUInt32($header, 24)
    }
  }
  return [uint32]0
}

function Invoke-Logged {
  param(
    [string]$LogPath,
    [scriptblock]$Command
  )

  & $Command *>&1 | Tee-Object -FilePath $LogPath
  if($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code $LASTEXITCODE. See $LogPath"
  }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Python = Get-PythonCommand
if($StartVersion -eq 0) {
  $StartVersion = [uint32]((Get-ActiveAssetVersion) + 1)
}

Write-Host "W800 HTTP Range stress test"
Write-Host "  Count       : $Count"
Write-Host "  StartVersion: $StartVersion"
Write-Host "  HTTP        : http://$HttpHost`:$HttpPort"
Write-Host "  MQTT        : $BrokerHost`:$BrokerPort"
Write-Host "  AssetMode   : $(if($KeepStaticValues) { 'original' } else { 'masked' })"
Write-Host "  Logs        : $LogDir"

for($i = 0; $i -lt $Count; $i++) {
  $version = [uint32]($StartVersion + [uint32]$i)
  $prefix = "iter_{0:D2}_v{1}" -f ($i + 1), $version
  $buildLog = Join-Path $LogDir "$prefix.build.log"
  $verifyLog = Join-Path $LogDir "$prefix.verify.log"
  $updateLog = Join-Path $LogDir "$prefix.update.log"
  $started = Get-Date

  Write-Host ""
  Write-Host "Iteration $($i + 1)/$Count, version=$version"

  $previousVersion = $env:UI_ASSET_VERSION
  $env:UI_ASSET_VERSION = [string]$version
  try {
    $buildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $BuildScript)
    if(!$KeepStaticValues -or $ClearStaticValues) {
      $buildArgs += "-ClearStaticValues"
    }
    if($KeepStaticValues) {
      $buildArgs += "-KeepStaticValues"
    }
    Invoke-Logged -LogPath $buildLog -Command { powershell @buildArgs }
  } finally {
    if($null -eq $previousVersion) {
      Remove-Item Env:\UI_ASSET_VERSION -ErrorAction SilentlyContinue
    } else {
      $env:UI_ASSET_VERSION = $previousVersion
    }
  }

  $verifyArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $VerifyScript,
                  "-BaseUrl", "http://$HttpHost`:$HttpPort",
                  "-AssetFile", $AssetFile)
  if($FullHttpVerify) {
    $verifyArgs += "-Full"
  }
  Invoke-Logged -LogPath $verifyLog -Command { powershell @verifyArgs }

  Invoke-Logged -LogPath $updateLog -Command {
    & $Python $RequestScript `
      --broker-host $BrokerHost `
      --broker-port $BrokerPort `
      --http-host $HttpHost `
      --http-port $HttpPort `
      --asset $AssetFile `
      --timeout $TimeoutSeconds
  }

  $finalStatus = $null
  foreach($line in (Get-Content -LiteralPath $updateLog -Encoding utf8)) {
    if($line -match '^\{.*\}$') {
      try { $finalStatus = $line | ConvertFrom-Json } catch { }
    }
  }
  if($null -eq $finalStatus -or [uint32]$finalStatus.asset.version -ne $version) {
    throw "Update log does not contain final status for version $version. See $updateLog"
  }

  $elapsed = [int]((Get-Date) - $started).TotalSeconds
  $summaryItem = [pscustomobject]@{
    iteration = $i + 1
    version = $version
    elapsedSeconds = $elapsed
    activeSlot = [uint32]$finalStatus.asset.slot
    atBinaryAttempts = [uint32]$finalStatus.at.bin.try
    atBinarySuccesses = [uint32]$finalStatus.at.bin.ok
    atBinaryErrors = [uint32]$finalStatus.at.bin.hdr +
                     [uint32]$finalStatus.at.bin.cap +
                     [uint32]$finalStatus.at.bin.arm +
                     [uint32]$finalStatus.at.bin.to
    buildLog = $buildLog
    verifyLog = $verifyLog
    updateLog = $updateLog
  }
  $summaryItem | ConvertTo-Json -Compress | Add-Content -LiteralPath $Summary -Encoding ASCII

  if($DelaySeconds -gt 0 -and $i -lt ($Count - 1)) {
    Start-Sleep -Seconds $DelaySeconds
  }
}

Write-Host ""
Write-Host "Stress run completed."
Write-Host "Summary: $Summary"
