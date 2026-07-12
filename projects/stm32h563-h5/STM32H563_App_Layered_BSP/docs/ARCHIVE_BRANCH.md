# Layered BSP archive

This project is the preserved, independently buildable STM32H563 application that uses the explicit `bsp_*`, `mcu_*`, and `board_*` layering model.

It is intentionally kept on the dedicated Git branch `archive/stm32h563-app-modbus-layered-bsp` as a learning reference. Future personal projects may use the flatter `bsp_xxx.c/.h` profile, but this branch should retain the layered implementation and its architecture documents.

Generated Keil outputs, local IDE state, logs, caches, packaged binaries, downloaded manuals, and media ignored by the repository are not part of the Git snapshot. All source files required by the archived Keil project remain in this project directory.
