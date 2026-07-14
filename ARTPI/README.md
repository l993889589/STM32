# ART-Pi STM32H750 ThreadX Starter

This project is derived from the H743 ThreadX demo in `../demo`, while adapting the MCU and board wiring to the ART-Pi STM32H750XB.

## First bring-up behavior

- ThreadX startup flow follows the demo: `main` -> `system_init` -> `tx_kernel_enter` -> `tx_application_define` -> startup task -> BSP init -> application tasks. ThreadX owns SysTick throughout kernel operation; the application does not call `HAL_SuspendTick()` or `HAL_ResumeTick()`.
- UART4 is the log port exposed through the onboard ST-Link virtual COM port.
  - TX: PA0
  - RX: PI9
  - 115200, 8 data bits, no parity, 1 stop bit
  - The BSP uses a logical-port API and an interrupt-driven TX FIFO. RX has no
    second FIFO: a single owner binds a block callback, which receives one byte
    per call today and can receive DMA blocks later without changing the app API.
- Two active-low LEDs have separate ownership.
  - Blue LED PI8 is toggled every 500 ms by a ThreadX heartbeat task. The task
    does not print anything to UART.
  - Red LED PC15 is available as a Modbus-controlled output.
- The INDUSTRY-IO board is initialized with the same BSP callback pattern as
  the demo project.
  - Active buzzer: PH7, active high, forced off before GPIO initialization
  - RS485 UART5 TX/RX: PB13/PB12, AF14, 115200 8N1
  - RS485 DE + /RE: PI4, low for receive and high for transmit
  - PI4 returns low only from the UART transmission-complete interrupt, after
    the final stop bit has left the MCU
- Timing resources are intentionally separated:
  - ThreadX owns SysTick at 1 kHz; thread-context millisecond delays sleep.
  - DWT CYCCNT provides blocking microsecond delays and pre-kernel delays.
  - TIM2 runs freely at 1 MHz and provides four independent one-shot compare callbacks.
  - TIM5_CH1 outputs PWM on expansion header P1 pin 32 (PH10). The onboard LEDs are not timer PWM pins.
- New project-owned functions use lower snake case. Required vendor and RTOS callback names retain their mandated spelling.

## Keil project

Open `Project/MDK-ARM(AC6)/art_pi_h750_threadx.uvprojx` with Keil MDK and build the `Flash` target.

The application is linked into the STM32H750 internal 128 KiB flash at `0x08000000`, so the first bring-up does not depend on external QSPI boot code.

## W25Q128 data flash layout and validation

The board data flash follows the official ART-Pi 16 MiB layout: 512 KiB Wi-Fi image, 512 KiB Bluetooth image, 2 MiB download area, 1 MiB EasyFlash area, and the remaining space for the filesystem. This project reserves the final 4 KiB sector at `0xFFF000` for explicit diagnostics, so its filesystem region ends before that sector.

Startup calculates standard CRC-32 values across the complete Wi-Fi and Bluetooth partitions and compares them with the official `Resource_16MB.bin` reference (`12BACAD0` and `5F4C7B70`). The destructive erase/write/read-back test is disabled by default. Set `APP_FLASH_DESTRUCTIVE_TEST_ENABLE` to `1U` only for an intentional hardware test. The test refuses to erase unless the reserved sector is already completely erased, writes a cross-page pattern, verifies it, then erases and verifies the sector again.

## LDC / Modbus RTU

The parent repository keeps two independent submodules:

- [`l993889589/ldc`](https://github.com/l993889589/ldc) at `ARTPI/ldc`
- [`l993889589/ld_modbus`](https://github.com/l993889589/ld_modbus) at `ARTPI/ld_modbus`

`app_modbus_rtu` binds UART5 RX interrupts to an LDC queue with a 1750 us idle
frame timeout. A ThreadX task, not the interrupt handler, consumes complete
frames and runs the `ld_modbus` server. The server uses unit ID 1 and exposes
16 elements in each Modbus table. Coils 0 and 1 control the red LED and buzzer;
the blue LED remains dedicated to the heartbeat. Input registers 0 through 7 contain board identity and live
communication diagnostics.

For a physical test, connect a USB-RS485 converter A-to-A, B-to-B, and GND-to-GND
without connecting its VCC pin. Then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\modbus_rtu_test.ps1 -Port COMxx
```

The script verifies RTU CRC, reads the board ID, writes and reads a holding
register, briefly exercises the red LED and buzzer, and restores both outputs
to off. Clone the parent repository with `--recurse-submodules`, or run
`git submodule update --init --recursive` after cloning.

## Expected output

After reset, open the ST-Link virtual COM port at 115200 baud. Startup prints
`hello`, performs one 100 ms buzzer self-test, verifies the external flash
images, scans Wi-Fi through AP6212, and finally reports that Modbus RTU is
ready. There is no periodic UART heartbeat output; the blue LED continues to
blink every 500 ms as the board-alive indicator.
