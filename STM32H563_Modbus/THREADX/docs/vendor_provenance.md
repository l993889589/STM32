# Vendor and reusable-library provenance

## STM32 vendor baseline

The initial vendor snapshot was copied without source edits from:

```text
C:/Users/99388/STM32Cube/Repository/STM32Cube_FW_H5_V1.5.1/Drivers/CMSIS
C:/Users/99388/STM32Cube/Repository/STM32Cube_FW_H5_V1.5.1/Drivers/STM32H5xx_HAL_Driver
```

The CubeMX clock seed selected STM32CubeH5 `1.5.1`. Each standalone project
keeps CMSIS under `Drivers/CMSIS` and HAL under
`Drivers/STM32H5xx_HAL_Driver`.

Vendor symbols and file naming are preserved inside the vendor boundary. Local
adaptation belongs in the flat `user/bsp/mcu_*` implementation and must not be implemented by modifying
vendor files.

## ThreadX baseline

The initial ThreadX snapshot was copied without source edits from:

```text
D:/Embedded/H5/STM32H563_App/Middlewares/ST/threadx/common
D:/Embedded/H5/STM32H563_App/Middlewares/ST/threadx/ports/cortex_m33/ac6
```

Only the ThreadX target compiles these sources. Common BSP, LDC, device drivers,
and the bare-metal target must not include ThreadX headers.

## LDC baseline

The complete LDC source was copied from:

```text
D:/Embedded/H5/STM32H563_App/user/ldc/core
```

It was previously verified to match the H563 project copy in
`https://github.com/l993889589/STM32` at commit:

```text
21658fcca87a23ed8c604f580940abb7259b5370
```

LDC remains an independent library. UART BSP transports bytes and events;
services own LDC contexts, rings, packet pools, and framing policy.

## ld_modbus upstream

The reusable protocol core is published independently at:

```text
https://github.com/l993889589/ld_modbus
```

The STM32 tree vendors the same `include/` and `src/` protocol implementation,
while board transports, W800, LDC ownership, Keil targets and hardware evidence
remain in this integration repository. Independent packaging, extended host/LDC
tests, contribution guidance and release CI are maintained in the upstream repo;
the validated upstream checkpoint for this integration is commit
`aa1bd99` (2026-07-11).
