param(
    [string]$Port = "COM19",
    [string]$Image = "firmware\ap6212\ap6212_qspi_bundle.bin",
    [int]$BaudRate = 115200,
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

$BaseAddress = 0x00700000
$EraseChunk = 0xF000
$WriteChunk = 224

function Get-Crc32([byte[]]$Data, [int]$Offset, [int]$Length) {
    [uint32]$crc = [uint32]::MaxValue
    for($i = 0; $i -lt $Length; $i++) {
        $crc = $crc -bxor [uint32]$Data[$Offset + $i]
        for($bit = 0; $bit -lt 8; $bit++) {
            if(($crc -band 1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor 0xEDB88320)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Put-U16LE([byte[]]$Buffer, [int]$Offset, [uint16]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    [Array]::Copy($bytes, 0, $Buffer, $Offset, 2)
}

function Put-U32LE([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    [Array]::Copy($bytes, 0, $Buffer, $Offset, 4)
}

function Get-U16LE([byte[]]$Buffer, [int]$Offset) {
    return [BitConverter]::ToUInt16($Buffer, $Offset)
}

function Get-U32LE([byte[]]$Buffer, [int]$Offset) {
    return [BitConverter]::ToUInt32($Buffer, $Offset)
}

function New-QspiFrame([byte]$Cmd, [uint16]$Seq, [uint32]$Address, [uint16]$Length, [uint32]$PayloadCrc, [byte[]]$Payload) {
    if($null -eq $Payload) { $Payload = [byte[]]::new(0) }
    $frame = [byte[]]::new(17 + $Payload.Length)
    $frame[0] = [byte][char]'Q'
    $frame[1] = [byte][char]'S'
    $frame[2] = [byte][char]'P'
    $frame[3] = [byte][char]'I'
    $frame[4] = $Cmd
    Put-U16LE $frame 5 $Seq
    Put-U32LE $frame 7 $Address
    Put-U16LE $frame 11 $Length
    Put-U32LE $frame 13 $PayloadCrc
    if($Payload.Length -gt 0) {
        [Array]::Copy($Payload, 0, $frame, 17, $Payload.Length)
    }
    return $frame
}

function Read-Ack($Serial, [byte]$Cmd, [uint16]$Seq) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $buffer = New-Object 'System.Collections.Generic.List[byte]'
    $tmp = [byte[]]::new(256)

    while([DateTime]::UtcNow -lt $deadline) {
        try {
            $n = $Serial.Read($tmp, 0, $tmp.Length)
            for($i = 0; $i -lt $n; $i++) { $buffer.Add($tmp[$i]) }
        } catch [TimeoutException] {
        }

        while($buffer.Count -ge 20) {
            $start = -1
            for($i = 0; $i -le $buffer.Count - 4; $i++) {
                if($buffer[$i] -eq [byte][char]'Q' -and
                   $buffer[$i + 1] -eq [byte][char]'A' -and
                   $buffer[$i + 2] -eq [byte][char]'C' -and
                   $buffer[$i + 3] -eq [byte][char]'K') {
                    $start = $i
                    break
                }
            }
            if($start -lt 0) {
                $buffer.RemoveRange(0, $buffer.Count - 3)
                break
            }
            if($start -gt 0) {
                $buffer.RemoveRange(0, $start)
            }
            if($buffer.Count -lt 20) { break }

            $ack = [byte[]]::new(20)
            $buffer.CopyTo(0, $ack, 0, 20)
            $buffer.RemoveRange(0, 20)
            $ackCmd = $ack[4]
            $status = $ack[5]
            $ackSeq = Get-U16LE $ack 6
            if($ackCmd -eq $Cmd -and $ackSeq -eq $Seq) {
                return [pscustomobject]@{
                    Cmd = $ackCmd
                    Status = $status
                    Seq = $ackSeq
                    Address = Get-U32LE $ack 8
                    AckValue = Get-U32LE $ack 12
                    Extra = Get-U32LE $ack 16
                }
            }
        }
    }
    throw "Timeout waiting for QSPI ACK cmd=$Cmd seq=$Seq"
}

function Send-QspiCommand($Serial, [byte]$Cmd, [uint16]$Seq, [uint32]$Address, [uint16]$Length, [byte[]]$Payload) {
    $crc = 0
    if($null -ne $Payload -and $Payload.Length -gt 0) {
        $crc = Get-Crc32 $Payload 0 $Payload.Length
    }
    $frame = New-QspiFrame $Cmd $Seq $Address $Length $crc $Payload
    $Serial.Write($frame, 0, $frame.Length)
    $Serial.BaseStream.Flush()
    Start-Sleep -Milliseconds 35
    $ack = Read-Ack $Serial $Cmd $Seq
    if($ack.Status -ne 0) {
        throw "Board returned status=$($ack.Status) cmd=$Cmd seq=$Seq addr=0x$($Address.ToString('X8')) value=0x$($ack.AckValue.ToString('X8'))"
    }
    return $ack
}

function Next-Seq([uint16]$Seq) {
    $next = [int]$Seq + 1
    if($next -gt 240) { $next = 1 }
    return [uint16]$next
}

$path = (Resolve-Path $Image).Path
$imageBytes = [IO.File]::ReadAllBytes($path)
$alignedLength = [int]([Math]::Ceiling($imageBytes.Length / 4096.0) * 4096)

$serial = New-Object System.IO.Ports.SerialPort $Port,$BaudRate,'None',8,'One'
$serial.ReadTimeout = 250
$serial.WriteTimeout = 2000

try {
    $serial.Open()
    Start-Sleep -Milliseconds 500
    $serial.DiscardInBuffer()

    [uint16]$seq = 1
    $info = Send-QspiCommand $serial 1 $seq 0 0 ([byte[]]::new(0)); $seq = Next-Seq $seq
    Write-Host ("INFO base=0x{0:X8} size=0x{1:X8}" -f $info.AckValue, $info.Extra)

    if(-not $VerifyOnly) {
        Write-Host ("ERASE length={0}" -f $alignedLength)
        for($offset = 0; $offset -lt $alignedLength; $offset += $EraseChunk) {
            $len = [Math]::Min($EraseChunk, $alignedLength - $offset)
            [void](Send-QspiCommand $serial 2 $seq ([uint32]($BaseAddress + $offset)) ([uint16]$len) ([byte[]]::new(0)))
            $seq = Next-Seq $seq
        }

        Write-Host ("WRITE length={0}" -f $imageBytes.Length)
        for($offset = 0; $offset -lt $imageBytes.Length; $offset += $WriteChunk) {
            $len = [Math]::Min($WriteChunk, $imageBytes.Length - $offset)
            $payload = [byte[]]::new($len)
            [Array]::Copy($imageBytes, $offset, $payload, 0, $len)
            [void](Send-QspiCommand $serial 3 $seq ([uint32]($BaseAddress + $offset)) ([uint16]$len) $payload)
            $seq = Next-Seq $seq
            if(($offset % 16384) -eq 0) {
                Write-Host ("  wrote {0}/{1}" -f $offset, $imageBytes.Length)
            }
        }
    }

    Write-Host "VERIFY"
    for($offset = 0; $offset -lt $imageBytes.Length; $offset += $EraseChunk) {
        $len = [Math]::Min($EraseChunk, $imageBytes.Length - $offset)
        $ack = Send-QspiCommand $serial 4 $seq ([uint32]($BaseAddress + $offset)) ([uint16]$len) ([byte[]]::new(0))
        $seq = Next-Seq $seq
        $expected = Get-Crc32 $imageBytes $offset $len
        if($ack.AckValue -ne $expected) {
            throw ("CRC mismatch offset=0x{0:X} board=0x{1:X8} host=0x{2:X8}" -f $offset, $ack.AckValue, $expected)
        }
    }

    Write-Host "DONE"
} finally {
    if($serial.IsOpen) { $serial.Close() }
}
