param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$BaudRate = 115200,
    [byte]$UnitId = 1,
    [int]$StressIterations = 100
)

$ErrorActionPreference = "Stop"

function get_modbus_crc([byte[]]$data)
{
    [uint32]$crc = 0xFFFF

    foreach ($value in $data)
    {
        $crc = $crc -bxor $value
        for ($bit = 0; $bit -lt 8; $bit++)
        {
            if (($crc -band 1) -ne 0)
            {
                $crc = (($crc -shr 1) -bxor 0xA001) -band 0xFFFF
            }
            else
            {
                $crc = ($crc -shr 1) -band 0xFFFF
            }
        }
    }

    return [uint16]$crc
}

function format_hex([byte[]]$data)
{
    return (($data | ForEach-Object { $_.ToString("X2") }) -join " ")
}

function invoke_modbus_request(
    [System.IO.Ports.SerialPort]$serial_port,
    [byte[]]$pdu,
    [bool]$show_frame = $true,
    [bool]$allow_exception = $false)
{
    $frame = [System.Collections.Generic.List[byte]]::new()
    $frame.Add($UnitId)
    foreach ($value in $pdu)
    {
        $frame.Add($value)
    }

    $crc = get_modbus_crc $frame.ToArray()
    $frame.Add([byte]($crc -band 0xFF))
    $frame.Add([byte](($crc -shr 8) -band 0xFF))

    $serial_port.DiscardInBuffer()
    $serial_port.Write($frame.ToArray(), 0, $frame.Count)

    $response = [System.Collections.Generic.List[byte]]::new()
    $expected_length = 2
    $timer = [System.Diagnostics.Stopwatch]::StartNew()

    while (($response.Count -lt $expected_length) -and ($timer.ElapsedMilliseconds -lt 1000))
    {
        try
        {
            $response.Add([byte]$serial_port.ReadByte())
        }
        catch [System.TimeoutException]
        {
            continue
        }

        if ($response.Count -ge 2)
        {
            $function_code = $response[1]
            if (($function_code -band 0x80) -ne 0)
            {
                $expected_length = 5
            }
            elseif (($function_code -ge 1) -and ($function_code -le 4))
            {
                if ($response.Count -ge 3)
                {
                    $expected_length = 5 + $response[2]
                }
                else
                {
                    $expected_length = 3
                }
            }
            else
            {
                $expected_length = 8
            }
        }
    }

    if ($response.Count -ne $expected_length)
    {
        throw "Response timeout: received $($response.Count)/$expected_length bytes: $(format_hex $response.ToArray())"
    }

    $response_bytes = $response.ToArray()
    $body = $response_bytes[0..($response_bytes.Length - 3)]
    [uint32]$crc_low = $response_bytes[-2]
    [uint32]$crc_high = $response_bytes[-1]
    $response_crc = [uint16]($crc_low -bor ($crc_high -shl 8))
    if ((get_modbus_crc $body) -ne $response_crc)
    {
        throw "Bad response CRC: $(format_hex $response_bytes)"
    }
    if ($response_bytes[0] -ne $UnitId)
    {
        throw "Unexpected unit id: $($response_bytes[0])"
    }
    $is_exception = (($response_bytes[1] -band 0x80) -ne 0)
    if ($is_exception -and (-not $allow_exception))
    {
        throw "Modbus exception function=0x$($response_bytes[1].ToString('X2')) code=0x$($response_bytes[2].ToString('X2'))"
    }
    if ((-not $is_exception) -and ($response_bytes[1] -ne $pdu[0]))
    {
        throw "Unexpected function code: 0x$($response_bytes[1].ToString('X2'))"
    }

    if ($show_frame)
    {
        Write-Host "TX: $(format_hex $frame.ToArray())"
        Write-Host "RX: $(format_hex $response_bytes)"
    }
    return ,$response_bytes
}

function assert_bad_crc_is_ignored(
    [System.IO.Ports.SerialPort]$serial_port)
{
    [byte[]]$body = @($UnitId, 0x03, 0x00, 0x00, 0x00, 0x01)
    $crc = get_modbus_crc $body
    [byte[]]$frame = @(
        $body[0], $body[1], $body[2], $body[3], $body[4], $body[5],
        ($crc -band 0xFF), ((($crc -shr 8) -band 0xFF) -bxor 0x01)
    )

    $serial_port.DiscardInBuffer()
    $serial_port.Write($frame, 0, $frame.Length)
    Start-Sleep -Milliseconds 100
    if ($serial_port.BytesToRead -ne 0)
    {
        throw "Bad-CRC request unexpectedly produced $($serial_port.BytesToRead) response bytes"
    }

    Write-Output "PASS: bad-CRC request was ignored without a response."
}

function write_single_coil(
    [System.IO.Ports.SerialPort]$serial_port,
    [uint16]$address,
    [bool]$on)
{
    [uint16]$value = if ($on) { 0xFF00 } else { 0x0000 }
    [byte[]]$pdu = @(
        0x05,
        (($address -shr 8) -band 0xFF),
        ($address -band 0xFF),
        (($value -shr 8) -band 0xFF),
        ($value -band 0xFF)
    )
    [void](invoke_modbus_request $serial_port $pdu)
}

$serial_port = [System.IO.Ports.SerialPort]::new(
    $Port,
    $BaudRate,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial_port.ReadTimeout = 50
$serial_port.WriteTimeout = 500
$serial_port.Handshake = [System.IO.Ports.Handshake]::None
$serial_port.DtrEnable = $false
$serial_port.RtsEnable = $false

try
{
    $serial_port.Open()

    Write-Output "Reading input registers 0..7 (function 0x04)..."
    [byte[]]$read_inputs = @(0x04, 0x00, 0x00, 0x00, 0x08)
    $input_response = invoke_modbus_request $serial_port $read_inputs
    if (($input_response[3] -ne 0x07) -or ($input_response[4] -ne 0x50))
    {
        throw "Board ID mismatch in input register 0"
    }

    Write-Output "Writing holding register 0, then reading it back..."
    [byte[]]$write_register = @(0x06, 0x00, 0x00, 0x12, 0x34)
    [void](invoke_modbus_request $serial_port $write_register)
    [byte[]]$read_holding = @(0x03, 0x00, 0x00, 0x00, 0x01)
    $holding_response = invoke_modbus_request $serial_port $read_holding
    if (($holding_response[3] -ne 0x12) -or ($holding_response[4] -ne 0x34))
    {
        throw "Holding register readback mismatch"
    }

    Write-Output "Checking illegal-address exception and bad-CRC rejection..."
    [byte[]]$illegal_address = @(0x03, 0x00, 0x10, 0x00, 0x01)
    $exception_response = invoke_modbus_request $serial_port $illegal_address $true $true
    if (($exception_response[1] -ne 0x83) -or ($exception_response[2] -ne 0x02))
    {
        throw "Expected illegal-data-address exception 0x83/0x02"
    }
    assert_bad_crc_is_ignored $serial_port

    if ($StressIterations -gt 0)
    {
        Write-Output "Running $StressIterations holding-register write/read stress iterations..."
        for ($iteration = 0; $iteration -lt $StressIterations; $iteration++)
        {
            [uint16]$stress_value = (0x4000 + $iteration) -band 0xFFFF
            [byte[]]$stress_write = @(
                0x06, 0x00, 0x01,
                (($stress_value -shr 8) -band 0xFF),
                ($stress_value -band 0xFF)
            )
            [void](invoke_modbus_request $serial_port $stress_write $false)
            [byte[]]$stress_read = @(0x03, 0x00, 0x01, 0x00, 0x01)
            $stress_response = invoke_modbus_request $serial_port $stress_read $false
            [uint16]$read_value = ([uint16]$stress_response[3] -shl 8) -bor $stress_response[4]
            if ($read_value -ne $stress_value)
            {
                throw "Stress mismatch at iteration ${iteration}: wrote $stress_value read $read_value"
            }
            if ((($iteration + 1) % 25) -eq 0)
            {
                Write-Output "  $($iteration + 1)/$StressIterations iterations passed"
            }
        }
    }

    Write-Output "Exercising red LED and buzzer coils while blue heartbeat continues..."
    write_single_coil $serial_port 0 $true
    Start-Sleep -Milliseconds 150
    write_single_coil $serial_port 0 $false
    try
    {
        write_single_coil $serial_port 1 $true
        Start-Sleep -Milliseconds 100
    }
    finally
    {
        write_single_coil $serial_port 1 $false
    }

    Write-Output "PASS: Modbus RTU functional, exception, CRC, stress, red LED, and buzzer checks completed."
}
finally
{
    if ($serial_port.IsOpen)
    {
        $serial_port.Close()
    }
    $serial_port.Dispose()
}
