# Project Index

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

The original workspace also contains `F4`, `H7`, `wds`, `cs`, `STM32_CYKJ`, manuals, and backups. These were not copied into the public tree because they appear to contain mixed reference packages, large third-party material, generated output, or unclear publication status.
