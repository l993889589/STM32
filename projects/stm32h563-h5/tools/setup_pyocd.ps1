param(
  [string]$PythonVersion = "3.12.4",
  [string]$PyOcdPackage = "pyocd"
)

$ErrorActionPreference = "Stop"

$ToolsRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$DownloadDir = Join-Path $ToolsRoot "downloads"
$PythonDir = Join-Path $ToolsRoot "python-$PythonVersion-embed-amd64"
$PythonZip = Join-Path $DownloadDir "python-$PythonVersion-embed-amd64.zip"
$GetPip = Join-Path $DownloadDir "get-pip.py"
$PythonExe = Join-Path $PythonDir "python.exe"
$PythonUrl = "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-amd64.zip"
$GetPipUrl = "https://bootstrap.pypa.io/get-pip.py"

New-Item -ItemType Directory -Path $DownloadDir -Force | Out-Null

if(!(Test-Path -LiteralPath $PythonExe)) {
  if(!(Test-Path -LiteralPath $PythonZip)) {
    Write-Host "Downloading Python $PythonVersion embeddable package..."
    Invoke-WebRequest -Uri $PythonUrl -OutFile $PythonZip
  }

  if(Test-Path -LiteralPath $PythonDir) {
    Remove-Item -LiteralPath $PythonDir -Recurse -Force
  }

  New-Item -ItemType Directory -Path $PythonDir -Force | Out-Null
  Expand-Archive -LiteralPath $PythonZip -DestinationPath $PythonDir -Force
}

$pth = Get-ChildItem -LiteralPath $PythonDir -Filter "python*._pth" | Select-Object -First 1
if($pth) {
  $pthText = Get-Content -LiteralPath $pth.FullName -Raw
  if($pthText -match "#import site") {
    $pthText = $pthText -replace "#import site", "import site"
    Set-Content -LiteralPath $pth.FullName -Value $pthText -NoNewline
  }
}

if(!(Test-Path -LiteralPath $GetPip)) {
  Write-Host "Downloading get-pip.py..."
  Invoke-WebRequest -Uri $GetPipUrl -OutFile $GetPip
}

Write-Host "Installing pip..."
& $PythonExe $GetPip
if($LASTEXITCODE -ne 0) {
  throw "get-pip failed with exit code $LASTEXITCODE"
}

Write-Host "Installing pyOCD..."
& $PythonExe -m pip install -U $PyOcdPackage
if($LASTEXITCODE -ne 0) {
  throw "pyOCD install failed with exit code $LASTEXITCODE"
}

Write-Host "Checking pyOCD..."
& $PythonExe -m pyocd --version
if($LASTEXITCODE -ne 0) {
  throw "pyOCD check failed with exit code $LASTEXITCODE"
}

Write-Host "Project-local pyOCD is ready."
Write-Host "Python: $PythonExe"
