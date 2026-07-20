# dwin_protocol

`dwin_protocol` is a C99, static-memory DWIN DGUS protocol helper.

It provides:

- `5A A5 LEN` byte-stream framing and resynchronization;
- CRC-enabled `0x82` variable-write frame construction;
- CRC-16/Modbus calculation with explicit DWIN wire byte order;
- classification of the fixed plain and CRC write acknowledgements.

The library has no STM32, HAL, ThreadX, UART, DMA, LDC, or product-policy
dependency. Transport ownership, retries, online policy, and buzzer scheduling
remain in the consuming application.
