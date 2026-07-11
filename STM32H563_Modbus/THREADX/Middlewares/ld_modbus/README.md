# ld_modbus

`ld_modbus` is a small C99 Modbus RTU/TCP protocol core for embedded systems.
It supports client and server roles without heap allocation, operating-system
calls, sockets, UART handles, or global mutable protocol state.

## Supported function codes

- `01` Read Coils
- `02` Read Discrete Inputs
- `03` Read Holding Registers
- `04` Read Input Registers
- `05` Write Single Coil
- `06` Write Single Register
- `0F` Write Multiple Coils
- `10` Write Multiple Registers
- `16` Mask Write Register
- `17` Write/Read Multiple Registers

The core provides bounded RTU CRC and TCP MBAP codecs, stateless client PDU
builders/parsers, and a server that works on application-owned static tables.
Transport framing and timeouts remain outside the protocol core. The optional
`integrations/ldc` adapter demonstrates silence-delimited RTU server delivery.

## Build and test

```sh
cmake -S . -B build -DLD_MODBUS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

With MinGW on Windows:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

## Embedded integration

1. Compile the three files in `src/`.
2. Add `include/` to the compiler include path.
3. Allocate register/coil tables and ADU buffers in the application.
4. Let the UART/TCP transport determine complete frame boundaries.
5. Call the server processor or client parser only from task/superloop context.

The library never calls `malloc`, `calloc`, `realloc`, or `free`. Its Apache-2.0
license is in [LICENSE](LICENSE).

The STM32H563 reference integration and hardware evidence live in the
[`l993889589/STM32`](https://github.com/l993889589/STM32) repository.
