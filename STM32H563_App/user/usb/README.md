# USB device architecture

The USB device is split into transport and application responsibilities so the
project can stop depending on CubeMX-generated application glue.

## Phase 1: CDC console

- USBX and the STM32 DCD own enumeration and endpoint transfers.
- `usb_console` owns the active CDC instance and serialized writes.
- `shell` owns line editing, parsing, and command dispatch.
- The application routes OTA magic frames first, textual input to the shell,
  and non-text input to the legacy LDC data path.

## Phase 2: vendor bulk transport

The composite device now includes a DPUMP-compatible vendor interface with
dedicated EP3 bulk IN and OUT endpoints. The vendor transport carries framed
channels for OTA, stress tests, binary commands, and structured logs. CDC
remains a human-operated shell and is not used for high-rate binary traffic.

The current Windows development setup needs the third interface (`MI_02`) to be
bound to WinUSB with Zadig. CDC continues to use the standard Windows USB serial
driver. A Microsoft OS 2.0 descriptor will replace this one-time binding step in
the desktop-assistant integration phase.

### Vendor frame

All integer fields are little-endian.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | Magic `LDV1` |
| 4 | 1 | Channel |
| 5 | 1 | Flags: bit 0 response, bit 1 error |
| 6 | 2 | Sequence |
| 8 | 4 | Payload length, maximum 1024 |
| 12 | 4 | IEEE CRC32 of payload |
| 16 | N | Payload |

Channels are control 0, LDC 1, OTA 2, stress 3, and log 4. Control operation 1
is ping and operation 2 returns device information. Stress responses contain
total frame count, total byte count, and the received payload CRC32. OTA channel
payloads retain the existing `LDOT` OTA packet format and acknowledgements are
returned inside the OTA vendor channel.

## Phase 3: remove CubeMX ownership

Move the device descriptor, PMA allocation, PCD setup, USBX initialization,
and class registration into `user/usb`. Keep the STM32 HAL PCD driver, USBX,
and ThreadX. Generated USB files can then be removed from the build without
changing the shell or application command code.

Planned endpoint allocation:

- EP0: control
- EP1 OUT/IN: CDC data
- EP2 IN: CDC notifications
- EP3 OUT/IN: vendor bulk data

CDC and vendor traffic must use separate queues and worker threads. The vendor
protocol must include channel, sequence, payload length, and CRC fields.
