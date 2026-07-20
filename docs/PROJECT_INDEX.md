# Project Index

## `projects/stm32f401-chpm`

CHPM STM32F401CCU6 product firmware using ThreadX and USBX CDC ACM.

Relevant areas:

- `Core/`: startup, ThreadX application entry, USB CDC parsing, and runtime owners
- `user/bsp/`: flat one-module-per-peripheral board support
- `user/app/`, `user/services/`, `user/dwin/`: product policy and communication services
- `third_party/dwin_protocol/`, `third_party/sensors/`: project-owned reusable pure C libraries
- `third_party/ldc/`, `third_party/ld_modbus/`: pinned public Git submodules
- `docs/`: board manifest, data flow, protocol review, storage layout, and validation notes

Current status:

- Keil ARMClang rebuild: 0 errors, 0 warnings.
- Static validation: 612 checks passed.
- Host tests: 11/11 passed.
- Source publication excludes build output, local work folders, binaries, and private Git metadata.
- Hardware timing, USB/DWIN stress behavior, RS485 electrical behavior, and sensor accuracy still require board validation.

## `projects/stm32h563-h5`

Main STM32H563 application and bootloader workspace.

Relevant areas:

- `STM32H563_App/`: application firmware
- `STM32H563_Bootloader/`: bootloader and OTA-related code
- `docs/`: architecture and issue notes
- `user/`: board support, protocol, AT modules, UI, and application code

Current status:

- Migrated from a local/Gitee-managed development tree.
- Contains active changes in the original workspace.
- Should be reviewed before tagging as stable.

## `boards/art-pi-stm32h750`

ART-Pi / STM32H750 experiments.

Relevant areas:

- AP6212 Wi-Fi/BT bring-up
- UART / LDC experiments
- USBX and NetX Duo integration
- board-level BSP and application services

## `libraries/ldc`

Small standalone LDC communication library.

Original Gitee remote found in the source workspace:

- `git@gitee.com:leduoc/ldc.git`

This is a good candidate to split into its own focused GitHub repository later.

## `modules/ec20-stm32f407`

STM32F407 + EC20 modem experiments.

Relevant areas:

- AT command workflow
- TCP/MQTT connection flow
- EC20 protocol notes
- Keil MDK project files

Needs cleanup before being presented as a stable reusable modem module.

## `examples/ldc-artpi`

ART-Pi example projects for LDC communication.

Includes variants with DMA / non-DMA and RTOS / non-RTOS setups.

## Not Included Yet

The original workspace also contains additional `F4`, `H7`, `wds`, `cs`,
manual, and backup trees. Those were not copied because they contain mixed
reference packages, generated output, large third-party material, or unclear
publication status.
