<#
.SYNOPSIS
Removes local Keil output and user-state files from the Bootloader project.

.DESCRIPTION
Run after a local build when a source-only Bootloader tree is required. The
script deletes only explicit paths below MDK-ARM and never touches source,
project, startup, CubeMX, shared OTA or vendor dependency files.
#>
param()

$ErrorActionPreference = "Stop"
$BootRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$MdkRoot = Join-Path $BootRoot "MDK-ARM"
$ExpectedRoot = [IO.Path]::GetFullPath($BootRoot).TrimEnd("\") + "\"

function Remove-BootGeneratedPath([string]$Path)
{
    $FullPath = [IO.Path]::GetFullPath($Path)
    if(!$FullPath.StartsWith($ExpectedRoot, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to remove path outside Bootloader: $FullPath"
    }
    if(Test-Path -LiteralPath $FullPath)
    {
        Remove-Item -LiteralPath $FullPath -Recurse -Force
    }
}

Remove-BootGeneratedPath (Join-Path $MdkRoot "STM32H563_Bootloader")
Remove-BootGeneratedPath (Join-Path $MdkRoot "DebugConfig")
Remove-BootGeneratedPath (Join-Path $MdkRoot "RTE")

Get-ChildItem -LiteralPath $MdkRoot -File -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Extension -eq ".log" -or
        $_.Name -like "*.uvguix.*" -or
        $_.Extension -eq ".uvoptx" -or
        $_.Extension -eq ".scvd"
    } |
    ForEach-Object { Remove-BootGeneratedPath $_.FullName }

Write-Host "Bootloader generated files removed."

