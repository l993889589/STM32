<# Verifies the generated package against the exact public key compiled into Boot. #>
param(
    [string]$Image = "",
    [string]$Manifest = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if([string]::IsNullOrWhiteSpace($Image)) {
    $Image = Join-Path $Root "ota_package\usb_ota_app.bin"
}
if([string]::IsNullOrWhiteSpace($Manifest)) {
    $Manifest = Join-Path $Root "ota_package\usb_ota_app.manifest.json"
}

$PublicX = "2ECF50B0BC6E2FB349FB632EF912637B47506B39A77708EE5C136B31718FF6B3"
$PublicY = "9CCDA807259DD724B7ADFA1547A335E5CD21B05248032F6D0683E877DE8A3926"
$Signer = Join-Path $Root "tools\ota_sign.py"
python $Signer verify --public-x $PublicX --public-y $PublicY --image $Image --manifest $Manifest
if($LASTEXITCODE -ne 0) {
    throw "OTA signature verification failed with exit code $LASTEXITCODE"
}

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("h563-ota-sign-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null
try {
    $TamperedManifest = Join-Path $TempRoot "tampered.manifest.json"
    $manifestObject = Get-Content -Raw $Manifest | ConvertFrom-Json
    $manifestObject.image_version = [uint32]([uint32]$manifestObject.image_version + 1)
    $tamperedJson = $manifestObject | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($TamperedManifest, $tamperedJson, [System.Text.UTF8Encoding]::new($false))

    $savedErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    python $Signer verify --public-x $PublicX --public-y $PublicY --image $Image --manifest $TamperedManifest 2>$null
    $tamperedManifestExit = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorAction
    if($tamperedManifestExit -eq 0) {
        throw "Tampered manifest unexpectedly passed signature verification"
    }

    $TamperedImage = Join-Path $TempRoot "tampered.bin"
    $imageBytes = [System.IO.File]::ReadAllBytes($Image)
    if($imageBytes.Length -eq 0) {
        throw "OTA test image is empty"
    }
    $tamperIndex = [int][Math]::Floor($imageBytes.Length / 2)
    $imageBytes[$tamperIndex] = $imageBytes[$tamperIndex] -bxor 0x5A
    [System.IO.File]::WriteAllBytes($TamperedImage, $imageBytes)

    $ErrorActionPreference = "Continue"
    python $Signer verify --public-x $PublicX --public-y $PublicY --image $TamperedImage --manifest $Manifest 2>$null
    $tamperedImageExit = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorAction
    if($tamperedImageExit -eq 0) {
        throw "Tampered image unexpectedly passed signature verification"
    }
}
finally {
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "OTA signing positive and tamper tests passed."
