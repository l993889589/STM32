# ART-Pi STM32H750 ThreadX Starter

This project is derived from the H743 ThreadX demo in `../demo`, while adapting the MCU and board wiring to the ART-Pi STM32H750XB.

## First bring-up behavior

- ThreadX startup flow follows the demo: `main` -> `system_init` -> suspend HAL tick -> `tx_kernel_enter` -> `tx_application_define` -> startup task -> BSP init -> application tasks.
- UART4 is the log port exposed through the onboard ST-Link virtual COM port.
  - TX: PA0
  - RX: PI9
  - 115200, 8 data bits, no parity, 1 stop bit
- Two active-low LEDs are driven by the LED task.
  - Blue LED: PI8
  - Red LED: PC15
- New project-owned functions use lower snake case. Required vendor and RTOS callback names retain their mandated spelling.

## Keil project

Open `Project/MDK-ARM(AC6)/art_pi_h750_threadx.uvprojx` with Keil MDK and build the `Flash` target.

The application is linked into the STM32H750 internal 128 KiB flash at `0x08000000`, so the first bring-up does not depend on external QSPI boot code.

## LDC / Modbus submodule

The repository publishes [`l993889589/ld_modbus`](https://github.com/l993889589/ld_modbus) at `ARTPI/ldc` as a Git submodule. Clone the parent repository with `--recurse-submodules`, or run `git submodule update --init --recursive` after cloning.

## Expected output

After reset, open the ST-Link virtual COM port at 115200 baud. The first line contains `hello`, followed by a periodic ThreadX heartbeat. The two onboard LEDs alternate every 500 ms.
