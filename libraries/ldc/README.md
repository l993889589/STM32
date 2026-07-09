# LDC

LDC is a small byte-stream framing library for MCU UART-style transports. The
caller owns the physical receive path, storage, locking callbacks, and protocol
parsing above complete frames.

Use LDC delimiter framing only when the entire byte stream is known to be text
or otherwise cannot contain the delimiter byte inside a payload. For mixed
AT-command streams that include exact-length binary payloads, keep delimiter
framing disabled and let the AT parser switch between line mode and bounded raw
mode.

See [AT binary stream framing note](docs/at_binary_stream_framing_note.md) for
the W800 HTTP Range incident that exposed this boundary.
