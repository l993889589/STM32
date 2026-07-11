$ErrorActionPreference = 'Stop'

$map = Join-Path $PSScriptRoot 'MDK-ARM\STM32H563_Modbus\STM32H563_Modbus.map'
if(!(Test-Path -LiteralPath $map))
{
    throw 'Keil map file is missing; image address cannot be verified'
}

$map_text = Get-Content -LiteralPath $map -Raw -Encoding Default
if($map_text -notmatch 'Load Region LR_IROM1 \(Base: 0x08000000,')
{
    throw 'Unsafe image: standalone load region does not start at 0x08000000'
}
if($map_text -notmatch '__Vectors\s+0x08000000')
{
    throw 'Unsafe image: vector table is not linked at 0x08000000'
}

Write-Output 'image_address_check: standalone 0x08000000 verified'
