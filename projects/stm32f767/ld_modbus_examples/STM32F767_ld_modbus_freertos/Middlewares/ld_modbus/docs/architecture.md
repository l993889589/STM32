# Architecture contract

## Purpose

`ld_modbus` owns Modbus wire framing and protocol semantics. Its optional RTU
framer owns the T1.5/T3.5 state machine, but it does not own UARTs, RS-485
direction, DMA, sockets, tasks, timers, clocks, or application business logic.

## Static ownership

- The application owns every buffer and mapping array.
- One execution context owns a mutable Modbus transaction context.
- The transport owns byte movement and end-of-character timestamps.
- `ld_modbus_rtu_framer` may turn timestamped bytes into strict RTU frame
  boundaries without depending on a particular timer or UART implementation.
- LDC may provide simpler RTU silence-based framing, but is not required by the
  core and does not by itself enforce the T1.5 invalid-frame rule.
- The server validates the complete address range before changing mapped data.

## Runtime model

The protocol core is frame-in/frame-out and never waits. Optional synchronous
client helpers may be built above it, while bare-metal and RTOS applications
can use the same codec and state-machine APIs.

## Optional strict RTU framer

The caller owns two static buffers and supplies each byte with its
end-of-character timestamp. At baud rates up to 19200, T1.5 and T3.5 are
calculated from the configured bits per character; above 19200 they are fixed
at 750 us and 1750 us. Because completion-to-completion timestamps include the
current character transmission time, the framer subtracts that character time
before comparing actual bus silence.

A T1.5 violation or buffer overflow invalidates the current stream. The framer
then discards bytes until a new T3.5 silence boundary, preventing a tail
fragment from being delivered as a fresh request. Hardware timestamp capture,
polling cadence, DMA extraction, and RTOS synchronization remain platform
responsibilities.

## Initial function matrix

| Function | Client | Server |
| --- | --- | --- |
| 01 Read Coils | yes | yes |
| 02 Read Discrete Inputs | yes | yes |
| 03 Read Holding Registers | yes | yes |
| 04 Read Input Registers | yes | yes |
| 05 Write Single Coil | yes | yes |
| 06 Write Single Register | yes | yes |
| 0F Write Multiple Coils | yes | yes |
| 10 Write Multiple Registers | yes | yes |
| 16 Mask Write Register | yes | yes |
| 17 Read/Write Multiple Registers | yes | yes |

## Complete-ADU entry points

- `ld_modbus_server_process_rtu_adu()` validates CRC, filters the unit address,
  applies permitted broadcast writes without replying, and emits a complete
  response frame.
- `ld_modbus_server_process_tcp_adu()` validates MBAP framing and preserves the
  transaction and unit identifiers in its response.
- Both functions are bounded frame-in/frame-out operations and never wait.

## Non-goals for v0.1

- ASCII transport;
- dynamic register-map discovery;
- transport-specific connection management;
- hidden retry threads;
- source-level compatibility with libmodbus or nanoMODBUS.
