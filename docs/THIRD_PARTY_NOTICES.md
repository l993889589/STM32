# Third-Party Notices

This repository contains or references code generated from, copied from, or built on top of third-party projects and vendor packages.

Known categories:

- STMicroelectronics CMSIS, HAL, STM32Cube-generated code, and middleware
- Azure RTOS / ThreadX / USBX / FileX / LevelX / NetX Duo related code where included by STM32Cube packages
- LVGL and GUIX related UI code where present
- cJSON and other small embedded libraries where present
- board-specific support code and reference startup/linker files
- CHPM pins LDC and `ld_modbus` through public Git submodules; their repository
  URLs and exact commits are recorded in `.gitmodules` and
  `projects/stm32f401-chpm/THIRD_PARTY_NOTICES.md`.

The top-level MIT License applies only to original code in this repository. Third-party files remain under their original licenses and copyright notices.

Before publishing a release, review every third-party directory and keep license files close to the corresponding source code.
