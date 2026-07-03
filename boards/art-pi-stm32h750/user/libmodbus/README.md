# STM32 Modbus RTU

This directory contains the STM32-only Modbus RTU slave core used by the H563
application. It has no POSIX, socket, heap, errno, FreeRTOS, or ThreadX
dependency.

## Ownership

- The UART BSP owns USART configuration and transmission.
- LDC owns byte-stream buffering and RTU silence-based frame boundaries.
- This library validates complete RTU ADUs, applies the register mapping, and
  generates responses.
- The application owns all mapping arrays and the `modbus_t` context.

## Integration

1. Declare static mapping storage and a static `modbus_t`.
2. Call `modbus_mapping_init()` with the application-owned arrays.
3. Call `modbus_rtu_slave_init()` with the unit address and transmit callback.
4. Pass each complete LDC frame to `modbus_rtu_slave_process()`.

The context contains its response buffer, so one context must only be processed
by one task at a time. Separate buses should use separate contexts.

Supported server functions are 01, 02, 03, 04, 05, 06, 07, 0F, 10, 11, 16,
and 17. Broadcast requests are accepted for write functions and never produce a
response.

The implementation and retained data helpers use SPDX-License-Identifier:
LGPL-2.1-or-later. Preserve the license notice when distributing firmware or
source derived from this directory.
