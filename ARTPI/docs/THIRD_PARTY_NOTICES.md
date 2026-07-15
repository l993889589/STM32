# Third-Party Notices

This starter project includes vendor and RTOS source copied from the supplied H743 demo so the Keil project remains self-contained.

## Microsoft Azure RTOS ThreadX

- Location: `ThreadX/`
- Upstream component: Azure RTOS ThreadX 6.0.1 as supplied by the demo
- License files: `ThreadX/LICENSE.txt` and `ThreadX/LICENSED-HARDWARE.txt`

## STMicroelectronics CMSIS device support

- Location: `Libraries/CMSIS/`
- Upstream component: STM32H7 CMSIS core and device support as supplied by the demo
- Copyright and redistribution terms are retained in the individual source and header files.

## STMicroelectronics STM32H7 HAL driver

- Location: `Libraries/STM32H7xx_HAL_Driver/`
- Upstream component: STM32H7 HAL driver as supplied by the demo
- License file: `Libraries/STM32H7xx_HAL_Driver/License.md`

## Eclipse ThreadX NetX Duo

- Location: `NetXDuo/`
- Upstream: https://github.com/eclipse-threadx/netxduo
- Version: `v6.1.7_rel`
- Commit: `c187d60014b149ee7c5e79b4d84c416caddbdef7`
- Imported paths: `NetXDuo/common`, `NetXDuo/ports/cortex_m7/ac5`
- License files: `NetXDuo/LICENSE.txt` and `NetXDuo/LICENSED-HARDWARE.txt`

The imported NetX Duo sources are kept unmodified; project-specific configuration is provided by `User/nx_user.h`.

## micro-ecc

- Location: `art_pi_h750_common/gateway_ota/micro_ecc/`
- Upstream: https://github.com/kmackay/micro-ecc
- Copyright: Kenneth MacKay
- License: BSD 2-Clause; see `art_pi_h750_common/gateway_ota/micro_ecc/LICENSE.txt`

The gateway bootloader uses micro-ecc only for secp256r1 firmware-signature verification.
