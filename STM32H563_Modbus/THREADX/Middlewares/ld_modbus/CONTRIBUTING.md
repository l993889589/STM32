# Contributing

Contributions are welcome through focused issues and pull requests.

- Keep the protocol core C99 and independent of an MCU, RTOS, socket API, or heap.
- Use caller-owned buffers and return explicit `ld_modbus_status_t` values.
- Add tests for valid boundaries, malformed lengths, exception responses, and
  output-buffer limits whenever behavior changes.
- Run CMake, `ctest`, and the strict warning build before opening a pull request.
- Do not include Wi-Fi credentials, device identifiers, customer data, or build
  outputs in commits.

Hardware transports belong in integrations or downstream board projects; the
core remains responsible for Modbus validation and function semantics only.
