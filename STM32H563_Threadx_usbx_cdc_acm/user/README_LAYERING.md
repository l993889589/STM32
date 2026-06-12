# STM32H563 User Layering

This project now follows the same idea as the H7 reference BSP:

- `user/bsp`: board support package. Board pins, board devices, and hardware helpers live here.
- `user/ldc`: LDC transport/framing core only. It should not contain Modbus, AT, or board-specific code.
- `user/protocol/modbus`: Modbus RTU master/slave protocol code.
- `user/at/core`: generic AT line parser, URC table, and session helper.
- `user/at/modules`: module-specific AT drivers, such as W800 and EC20.
- `user/app`: application integration, tasks, OTA, MQTT packet helper, and LDC port configuration.

## BSP comparison

The H7 reference uses a board-level `bsp.h`/`bsp.c` entry plus per-device drivers under `bsp/inc` and `bsp/src`.
That is better than putting all board IO and protocol logic in one application file because:

- hardware pins and board devices are easier to port to a custom board;
- protocol code can be reused without dragging board GPIO/SPI/UART details with it;
- adding another UART, USB CDC endpoint, or AT module mostly touches configuration and a small driver.

The H563 project now uses the same direction, but scaled to the current board instead of copying the whole H7 BSP tree.

## Adding another LDC port

Add an item to `user/app/app_ldc_config.c` and allocate its `ldc_t`, ring buffer, packet pool, and RX restart function in the
application integration layer. Keep the frame policy in the config table.

Typical choices:

- Modbus RTU UART: `LDC_FRAME_POLICY_MODBUS_RTU`
- AT/text UART: `LDC_FRAME_POLICY_LENGTH_TIMEOUT` with delimiter `'\n'`
- USB CDC command stream: `LDC_FRAME_POLICY_LENGTH_TIMEOUT` or delimiter framing

## Adding another AT module

Create a new `at_module_xxx.c/.h` under `user/at/modules` and implement `at_module_driver_t`.
The application should call `at_module_probe`, `at_module_connect_network`, `at_module_open_socket`,
`at_module_send_socket`, and `at_module_close_socket` instead of hard-coding module commands.

W800 is implemented with WiFi commands. EC20 is included as a cellular socket driver skeleton using Quectel-style
`QICSGP/QIACT/QIOPEN/QISEND` commands.
