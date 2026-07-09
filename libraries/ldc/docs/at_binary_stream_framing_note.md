# AT Binary Stream Framing Note

This note records a 2026-07-09 field issue found while updating STM32H563 UI
assets through a W800 AT Wi-Fi module. The same rule applies to any AT module
that returns text headers followed by exact-length binary payloads.

## Problem

The W800 socket receive command returns a text header and then raw socket bytes:

```text
+OK=<size><CR><LF><CR><LF><binary payload>
```

The application originally configured LDC with newline delimiter framing for the
whole W800 UART stream:

```c
ldc_config.delimiter_enabled = true;
ldc_config.delimiter = '\n';
```

This is valid for pure AT text lines, but it is not valid once the same UART
stream carries socket payloads. HTTP Range image blocks legitimately contain
`0x0A` bytes. When the payload is LF-heavy, delimiter framing splits one binary
payload into many small LDC packets. The AT parser then loses its exact-length
raw receive boundary, packet descriptors churn, and the higher-level download
appears to fail at a deterministic asset offset.

Observed failure before the fix:

- Device received `ui_http_manifest_update`.
- HTTP Range download started normally.
- Transfer failed repeatedly around `received=106368` with
  `http.error="range retry"`.
- The failed asset area contained many `0x0A` bytes, which matched the delimiter
  packet storm theory.

## Boundary Rule

LDC owns byte-stream buffering and optional physical frame boundaries. It must
not guess protocol structure that belongs to the next layer.

Use delimiter framing only when all payload bytes obey the delimiter contract:

- shell commands terminated by newline;
- ASCII/UTF-8 protocol lines;
- sensor text frames with escaped or impossible delimiter bytes.

Do not use delimiter framing for mixed text/binary AT streams:

- W800 `SKRCV`;
- EC20/Quectel socket reads that return `+QIRD` style headers plus binary data;
- modem file reads or TLS socket reads with exact byte counts;
- any stream where `0x0D` or `0x0A` can appear inside payload data.

For those streams, configure LDC as a neutral byte collector:

```c
ldc_config.delimiter_enabled = false;
ldc_config.delimiter = 0U;
ldc_config.timeout_ms = small_idle_timeout_ms;
ldc_config.max_frame = enough_for_header_plus_raw_window;
```

Then let AT core parse text lines and explicitly enter raw mode after it has
seen a length-bearing header. Raw mode must copy exactly the announced payload
length and must not interpret CR, LF, or any other byte as a frame terminator.

## W800-Specific Detail

The W800 `SKRCV` response has an empty line between the `+OK=<len>` header and
the payload. The AT raw receive path should therefore skip the remaining
`<CR><LF>` after the header line before copying binary bytes.

The working parser model is:

1. AT line parser receives `+OK=<len>`.
2. Socket adapter calls raw receive begin with expected length.
3. AT core skips the empty line separator.
4. AT core copies exactly `<len>` bytes into the caller buffer.
5. Socket adapter validates higher-level CRC, size, or protocol headers before
   committing the data.

Socket IDs identify independent network channels inside the W800, but they do
not create independent UART command sessions. All AT commands and responses
still share one UART stream and must be serialized by one AT session owner.

## Fix Applied In The STM32H563 App

The W800 LDC configuration was changed from newline delimiter framing to neutral
stream collection. The AT core retained CR/LF line parsing for ordinary AT text
and used bounded raw mode for `SKRCV` payloads.

Relevant app-side shape:

```c
/* W800 UART carries both AT text and SKRCV binary payloads. */
ldc_config.delimiter_enabled = false;
ldc_config.delimiter = 0U;
```

The AT raw receive setup also documents the W800 separator:

```c
/* W800 socket reads return "+OK=<len><CR><LF><CR><LF>" before payload. */
raw_skip_eol = 1U;
```

## Verification

After disabling delimiter framing for the W800 UART stream, the same HTTP Range
asset update completed end to end:

- Firmware build ID: `Jul  9 2026 20:03:43`.
- Asset version: `2026070901`.
- Asset size: `1560576` bytes.
- Asset CRC32: `0x4CF16A04`.
- Final board status:
  - `asset.version=2026070901`;
  - `asset.received=1560576`;
  - `asset.expected=1560576`;
  - `asset.error="none"`;
  - `http.state=3`;
  - `http.error=""`.

This verifies that the fixed receive boundary can pass LF-heavy binary payloads
without falling back to MQTT chunks or changing the image data plane.

## Checklist For New AT Module Ports

- Keep one owner for the UART AT session.
- Treat socket IDs as module-internal channels, not concurrent UART sessions.
- Parse text lines in AT core or module adapter.
- Enter exact-length raw mode only after a trusted header announces payload
  length.
- Keep delimiter framing disabled for streams that can contain arbitrary binary.
- Validate binary data at the protocol/application layer before writing Flash.
- Expose counters for packet drops, raw receive length, socket receive result,
  and last payload head bytes during bring-up.
