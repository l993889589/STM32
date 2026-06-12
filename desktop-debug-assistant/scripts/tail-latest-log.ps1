$ErrorActionPreference = "Stop"

$projectLog = Join-Path $PSScriptRoot "..\logs\latest.log"
$packagedLog = Join-Path $PSScriptRoot "..\release\win-unpacked\logs\latest.log"

$candidates = @($projectLog, $packagedLog) | Where-Object { Test-Path $_ }

if ($candidates.Count -eq 0) {
  Write-Host "No latest.log found yet. Open a serial port in H5 Debug Assistant first."
  exit 0
}

$latest = $candidates |
  ForEach-Object { Get-Item $_ } |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

Write-Host "Tailing $($latest.FullName)"
Get-Content -LiteralPath $latest.FullName -Wait -Tail 80
