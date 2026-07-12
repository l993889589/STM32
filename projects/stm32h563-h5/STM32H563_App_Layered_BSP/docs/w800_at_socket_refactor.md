# W800 AT Socket And UI Asset Update Notes

## Purpose

This note records the working boundary for W800 AT sockets and the validated UI
asset update path. W800 `AT+SKRCV` returns a text header followed by raw socket
bytes, so raw payload must be handled by length and must not be parsed by the
normal CR/LF AT line parser.

## Implemented Boundary

- `at_core` owns UART byte ingestion and supports a temporary raw receive window.
- `at_session` owns command sequencing and drains stale text before arming a raw
  read after a matched header such as `+OK=<len>`.
- `at_module` exposes modem-independent socket APIs with explicit socket IDs:
  `open_socket_id`, `send_socket_id`, `recv_socket_id`, and `close_socket_id`.
- `at_module_w800` implements W800 `SKSND/SKRCV` semantics. `SKSND` sends raw
  payload after `+OK=<actualsize>`, then drains delayed command tail text before
  the next command. `SKRCV` reads only the advertised payload length.
- `app_w800` feeds MQTT and HTTP parsers only from socket receive APIs after the
  AT wrapper has been stripped.
- `app_w800` queries `AT+SKSTT=<socket>` before `AT+SKRCV` and only reads when
  the W800 reports nonzero `rx_data`. This avoids empty receive commands and
  reduces stale text in the shared AT stream.
- Socket IDs identify W800 network channels, but all commands and responses
  still share one UART AT session. HTTP and MQTT operations must therefore be
  serialized by the W800 task; they are not independent concurrent transports.
- W800 must be hardware-reset through `bsp_w800_hard_reset()` during startup.
  That reset line is `PC9`; if `PC9` is not configured as GPIO output, the W800
  module can preserve an old TCP/MQTT socket while the MCU firmware changes.

## Transfer Strategy

The accepted UI update path is:

1. Desktop assistant serves `manifest.json` and `ui_assets.bin` over HTTP.
2. Device receives an MQTT `ui_http_manifest_update` command.
3. Device downloads the asset with HTTP Range requests into the inactive
   external Flash slot.
4. Each HTTP block carries a server-side `X-Range-CRC32`; failed blocks are not
   written.
5. When W800 repeatedly corrupts a small HTTP binary block, firmware shrinks the
   Range block down to 64 bytes and retries. If the bounded retry limit is
   exhausted, the update fails with `range retry` and the previous valid UI slot
   remains active.
6. The firmware verifies full-package CRC from external Flash before committing
   the slot.

The W800 AT UART can still corrupt some binary HTTP ranges. The important part
is that block CRC catches those ranges before Flash write. MQTT is no longer
used as an implicit image data fallback; it remains the command/status plane.

## External Flash Rule

UI resources use the GD25LQ128 A/B layout from `ota_layout.h`:

```text
Slot A: 0x00500000 - 0x009FFFFF
Slot B: 0x00A00000 - 0x00EFFFFF
Size  : 5 MiB each
```

The GD25 driver reads through short full-duplex SPI transactions
(`command + dummy + data`) and retries failures. This is required because a
long final CRC pass previously failed with transient SPI read errors even after
the network transfer had completed correctly.

## Validation Result

Validated on 2026-07-09 with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build
```

Observed result after publishing `ui_http_manifest_update`:

```text
fwBuildId     : Jul  9 2026 16:04:21
asset.version : 2026070701
asset.received: 1560576
asset.expected: 1560576
asset.error   : none
http.state    : 3
http.error    : ""
```

This confirmed the build/flash workflow, W800 socket boundary, HTTP Range
transfer, external Flash write, Flash readback CRC, and A/B commit path for that
build. Later firmware revisions removed the automatic MQTT data fallback so the
production path is pure HTTP Range plus MQTT command/status.

Final firmware identity validation after fixing the app-range erase and W800
reset pin:

```text
fwBuildId     : Jul  9 2026 17:34:10
clientId      : leduo-h563-w800
deviceId      : leduo-h563-w800
asset.version : 2026070701
asset.error   : none
```

The root cause of the previous `leduo-h563-w800-codex` status was not MQTT
parsing. The internal Flash still contained stale bytes from an older app image,
and W800 reset was ineffective because `PC9` was not initialized. The daily flash
script now erases the complete app range before programming, and `PC9` is part
of GPIO initialization.
