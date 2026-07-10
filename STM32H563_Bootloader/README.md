# STM32H563 Bootloader

This is the minimal production Boot project for the H563 board. It owns secure
startup, external firmware A/B validation, internal App installation, bounded
trial rollback, diagnostics and independent USB CDC recovery.

## Build

Open `MDK-ARM/STM32H563_Threadx_usbx_cdc_acm.uvprojx` in Keil or run the root
`flash_ota_all.ps1` workflow. A clean build recreates the ignored
`MDK-ARM/STM32H563_Bootloader/` output directory.

## Kept Dependencies

- CMSIS headers for Cortex-M33 and STM32H563 only.
- HAL modules explicitly listed by the Keil target.
- ThreadX Cortex-M33 AC6 port and common sources used by the target.
- USBX device core, STM32 DCD and CDC ACM class only.
- Project-owned Boot, shell, LDC, GD25LQ128 and OTA sources.
- Shared OTA contracts and micro-ecc from the workspace `shared/` directory.

CMSIS DSP/NN/DAP/Core-A, unused HAL modules, unused RTOS/USBX ports, examples,
Keil logs, target output, debugger state and `.uvoptx` files are intentionally
excluded. The `.ioc` file remains as pin/clock evidence; CubeMX regeneration may
restore vendor files and must be followed by a reviewed clean build.

