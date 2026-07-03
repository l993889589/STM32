# STM32 Embedded Projects

This repository is a curated public archive of STM32 embedded projects and reusable communication components.

The source projects were originally developed and maintained locally and partly on Gitee. This GitHub repository is organized for public review, reuse, and long-term open-source maintenance.

## Highlights

- STM32H563 application and bootloader work based on ThreadX, USBX, FileX, LevelX, NetX Duo, LVGL, GUIX, and Keil MDK.
- Reusable LDC byte-stream framing and communication components for UART/RS485-style embedded links.
- EC20 modem experiments on STM32F407, including AT command handling and MQTT/TCP workflow notes.
- ART-Pi STM32H750 board bring-up experiments, AP6212 Wi-Fi/BT notes, USBX, NetX Duo, and LDC integration.
- Keil MDK and STM32CubeMX project files kept where useful for rebuilding and hardware verification.

## Repository Layout

```text
boards/
  art-pi-stm32h750/        ART-Pi / STM32H750 board experiments

examples/
  ldc-artpi/               LDC ART-Pi example projects

libraries/
  ldc/                     Standalone LDC communication library

modules/
  ec20-stm32f407/          EC20 modem / STM32F407 experiments

projects/
  stm32h563-h5/            STM32H563 application and bootloader work

docs/
  MIGRATION.md             Gitee/local history and migration notes
  PROJECT_INDEX.md         Project index and current status
  PUBLICATION_CHECKLIST.md Public release checklist
```

## What Is Included

The public tree keeps source code, project configuration, board/application notes, and build scripts where they are relevant.

The following were intentionally excluded from the first public pass:

- local backups and temporary folders
- PDFs, manuals, spreadsheets, and reference material with unclear redistribution rights
- Keil build outputs such as `Objects`, `Listings`, `*.axf`, `*.hex`, `*.map`, `*.o`, and `*.d`
- packaged desktop tools, downloaded runtimes, installers, and generated binaries
- private Git metadata from the original local repositories

## Build Notes

Most firmware projects are Keil MDK / STM32CubeMX based.

Typical entry points:

- `projects/stm32h563-h5/STM32H563_App/MDK-ARM/STM32H563_Threadx_usbx_cdc_acm.uvprojx`
- `projects/stm32h563-h5/STM32H563_Bootloader/MDK-ARM/STM32H563_Threadx_usbx_cdc_acm.uvprojx`
- `boards/art-pi-stm32h750/MDK-ARM/ARTPI.uvprojx`
- `modules/ec20-stm32f407/MDK-ARM/stm32f407_ec20.uvprojx`

Some projects depend on STM32Cube-generated middleware, Azure RTOS components, LVGL/GUIX, and board-specific hardware. Hardware-level behavior has not been revalidated after this public cleanup.

## License

Original code in this repository is released under the MIT License unless a file or directory states otherwise.

Third-party components, vendor HAL/CMSIS code, middleware, LVGL/GUIX-related code, and copied reference code remain under their own licenses. See `docs/THIRD_PARTY_NOTICES.md`.

## Migration Status

This is a cleaned GitHub publication tree, not a raw copy of the original working directory. See `docs/MIGRATION.md` for context.
