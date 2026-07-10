<#
.SYNOPSIS
Builds a reproducible STM32H563 Boot/App factory and signed OTA release bundle.

.DESCRIPTION
The private signing key is read from its external path and is never copied to
the output. The generated directory contains programming HEX files, signed OTA
files, build logs, checksums and source revision metadata for factory traceability.
#>
param(
    [Parameter(Mandatory = $true)]
    [uint32]$ImageVersion,
    [string]$OutputRoot = "",
    [string]$SigningKey = "$env:USERPROFILE\.leduo\keys\h563_ota_private.pem",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Uv4 = "C:\Keil_v5\UV4\UV4.exe"
$MinimumVersion = [uint32]2026071000

if($ImageVersion -lt $MinimumVersion) {
    throw "ImageVersion $ImageVersion is below compiled minimum $MinimumVersion"
}
if(!(Test-Path -LiteralPath $SigningKey)) {
    throw "OTA signing key not found: $SigningKey"
}
if([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root "release\h563"
}

$ReleaseDir = Join-Path $OutputRoot ("v{0}" -f $ImageVersion)
if(Test-Path -LiteralPath $ReleaseDir) {
    throw "Release directory already exists: $ReleaseDir"
}
New-Item -ItemType Directory -Path $ReleaseDir -Force | Out-Null

function Invoke-KeilReleaseBuild([string]$Name, [string]$Project, [string]$Log) {
    <# Rebuilds one Keil target and accepts it only when the log reports zero errors. #>
    $process = Start-Process -FilePath $Uv4 `
        -ArgumentList @("-r", "`"$Project`"", "-j0", "-o", "`"$Log`"") `
        -WindowStyle Hidden -Wait -PassThru
    $buildOk = (Test-Path -LiteralPath $Log) -and
               (Select-String -LiteralPath $Log -Pattern "0 Error\(s\)" -Quiet)
    if(!$buildOk) {
        throw "$Name build failed; Keil exit code $($process.ExitCode)"
    }
}

$BootProject = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$AppProject = Join-Path $Root "STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$BootBuildLog = Join-Path $ReleaseDir "boot-build.log"
$AppBuildLog = Join-Path $ReleaseDir "app-build.log"

try {
    if(!$SkipBuild) {
        Invoke-KeilReleaseBuild "Boot" $BootProject $BootBuildLog
        Invoke-KeilReleaseBuild "App" $AppProject $AppBuildLog
    }

    $BootHex = Join-Path $Root "STM32H563_Bootloader\MDK-ARM\STM32H563_Bootloader\STM32H563_Bootloader.hex"
    $AppHex = Join-Path $Root "STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex"
    foreach($artifact in @($BootHex, $AppHex)) {
        if(!(Test-Path -LiteralPath $artifact)) { throw "Build artifact not found: $artifact" }
    }
    Copy-Item -LiteralPath $BootHex -Destination (Join-Path $ReleaseDir "bootloader.hex")
    Copy-Item -LiteralPath $AppHex -Destination (Join-Path $ReleaseDir "application.hex")

    $MakePackage = Join-Path $Root "make_ota_package.ps1"
    powershell -NoProfile -ExecutionPolicy Bypass -File $MakePackage `
        -ImageVersion $ImageVersion -PackageName "firmware" `
        -OutputDir $ReleaseDir -SigningKey $SigningKey
    if($LASTEXITCODE -ne 0) { throw "Signed OTA package generation failed" }

    $Manifest = Join-Path $ReleaseDir "firmware.manifest.json"
    $Image = Join-Path $ReleaseDir "firmware.bin"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "tools\test_ota_signing.ps1") `
        -Image $Image -Manifest $Manifest
    if($LASTEXITCODE -ne 0) { throw "Release signature verification failed" }

    $manifestObject = Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
    $revision = (git -C $Root rev-parse HEAD).Trim()
    $releaseInfo = [ordered]@{
        schema = 1
        imageVersion = $ImageVersion
        sourceRevision = $revision
        signingKeyId = [string]$manifestObject.signing_key_id
        bootBase = "0x08000000"
        appBase = "0x08020000"
        createdAtUtc = [DateTime]::UtcNow.ToString("o")
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText(
        (Join-Path $ReleaseDir "release-info.json"), $releaseInfo,
        [System.Text.UTF8Encoding]::new($false))

    $files = Get-ChildItem -LiteralPath $ReleaseDir -File | Sort-Object Name
    $checksums = foreach($file in $files) {
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        "$hash  $($file.Name)"
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path $ReleaseDir "SHA256SUMS.txt"), $checksums,
        [System.Text.UTF8Encoding]::new($false))

    Write-Host "H563 release bundle created: $ReleaseDir"
}
catch {
    Remove-Item -LiteralPath $ReleaseDir -Recurse -Force -ErrorAction SilentlyContinue
    throw
}
