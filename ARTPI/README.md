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

The AP6212 WICED integration links a proprietary vendor library that is not
redistributed by the parent repository. Before building a published clone,
place the matching ART-Pi library at
`Libraries/AP6212/libwifi_6212_armcm7_2.1.2_armcc.lib`; see
`docs/THIRD_PARTY_NOTICES.md` for the publication boundary.

The Qt/QML desktop workstation is published at
`Desktop/ArtPiGatewayStudio`. Its README covers CMake presets, P0-P3 features,
hardware regression, SQLite storage, MQTT limitations, and portable ZIP
packaging.

## W25Q128 data flash layout and validation

The board data flash follows the official ART-Pi 16 MiB layout: 512 KiB Wi-Fi image, 512 KiB Bluetooth image, a 2 MiB H563 relay-download area, and a 1 MiB EasyFlash area. The gateway's own OTA staging area occupies `0x400000..0x5FFFFF`; the filesystem starts at `0x600000`. This project reserves the final 4 KiB sector at `0xFFF000` for explicit diagnostics, so its filesystem region ends before that sector.

Startup calculates standard CRC-32 values across the complete Wi-Fi and Bluetooth partitions and compares them with the official `Resource_16MB.bin` reference (`12BACAD0` and `5F4C7B70`). The destructive erase/write/read-back test is disabled by default. Set `APP_FLASH_DESTRUCTIVE_TEST_ENABLE` to `1U` only for an intentional hardware test. The test refuses to erase unless the reserved sector is already completely erased, writes a cross-page pattern, verifies it, then erases and verifies the sector again.

## LDC / Modbus RTU

The parent repository keeps two independent submodules:

- [`l993889589/ldc`](https://github.com/l993889589/ldc) at `ARTPI/ldc`
- [`l993889589/ld_modbus`](https://github.com/l993889589/ld_modbus) at `ARTPI/ld_modbus`

`app_modbus_rtu` binds UART5 RX interrupts to an LDC queue with a 1750 us idle
frame timeout. One ThreadX owner task consumes complete frames and can run as
either an `ld_modbus` slave or a master for 1..10 devices. In master mode each
device has configurable coils, discrete-inputs, holding-registers, and
input-registers ranges. Three consecutive failures mark a device offline;
probes back off through 1, 2, 5, 10, 30, and 60 seconds, then continue every
60 or 300 seconds. Any successful response restores normal polling.

FC05/FC06 control requests use a 16-entry high-priority FIFO. They never
interrupt an in-flight RTU transaction, but dispatch before the next normal
poll. A maximum burst of four commands guarantees that due polling is not
starved. The slave mode exposes 16 elements in each Modbus table. Coils 0 and
1 control the red LED and buzzer; the blue LED remains dedicated to the
heartbeat.

For a physical test, connect a USB-RS485 converter A-to-A, B-to-B, and GND-to-GND
without connecting its VCC pin. Then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\modbus_rtu_test.ps1 -Port COMxx
```

The script verifies RTU CRC, reads the board ID, writes and reads a holding
register, briefly exercises the red LED and buzzer, and restores both outputs
to off. Clone the parent repository with `--recurse-submodules`, or run
`git submodule update --init --recursive` after cloning.

## Web configuration and ten-slave simulation

After DHCP is bound, browse to `http://<board-ip>/`. The page configures the
RS485 role, slave unit ID, 1..10 master devices, response timeouts, all four
register classes, long-term offline probe period, and local outputs. Runtime
state is available from `GET /api/rs485/status`; FC05/FC06 commands are queued
with `POST /api/rs485/command`. The complete configuration is stored in the
two CRC-protected W25Q128 slots at `0x3FE000` and `0x3FF000`.

To simulate ten physical slaves with the PC USB-RS485 adapter on COM3:

```powershell
python -m pip install pyserial
python .\Tools\modbus_rtu_slave_simulator.py --port COM3 --units 10
```

The simulator supports FC01/02/03/04/05/06. Edit
`Tools/modbus_sim_control.json` while it runs to add unit IDs to
`offline_units`, inject bad CRCs, add response delay, or freeze the generated
process values. By default, discrete inputs toggle slowly and the input and
non-overridden holding registers follow deterministic triangle waves at a
one-second step. FC05/FC06 write overrides remain stable. This provides a
repeatable way to validate trends, failure counting, offline backoff, recovery,
and priority writes without owning ten instruments.

The simulator is not part of the current physical gateway-to-H563 topology.
When the gateway RS485 port is wired directly to the H563, keep the persistent
master table at one device (unit ID 1); otherwise unreachable simulated units
only add artificial timeout latency.

## Signed gateway and H563 OTA

The gateway supports two independent update paths:

- H563 relay OTA uses custom Modbus function code `0x41`. Normal polling runs
  at 115200, while an OTA session negotiates 460800 and restores the business
  baud rate afterward. The gateway caches the signed H563 package in the SPI1
  W25Q128 download partition before forwarding it.
- Gateway self-update uses an internal-flash Stage-0 Boot, a fixed QSPI XIP
  execution area at `0x90000000`, two 2 MiB QSPI image slots, and two committed
  control-record sectors. The SPI1 W25Q128 staging package is not installed
  until `finish` and `start` are separate successful operations.

Open `Project/MDK-ARM(AC6)/art_pi_h750_qspi.uvprojx` and build `QSPI_App` for
the external-XIP application. The Boot project is kept beside this project at
`../art_pi_h750_boot`; it must be programmed only with the ART-Pi ST-LINK.
Do not use the H563 CMSIS-DAP probe for the H750 Boot.

Gateway HTTP endpoints are:

- `POST /api/ota/gateway/manifest`
- `POST /api/ota/gateway/chunk?offset=<n>`
- `POST /api/ota/gateway/finish`
- `POST /api/ota/gateway/start`
- `GET /api/ota/gateway/status`

`Tools/gateway_ota.py` packages and verifies ECDSA P-256 images and uploads
them in 7168-byte chunks. `upload --resume` resumes only when device version,
length, and CRC all match the local package. The signing private key stays
outside the repository; only the public trust anchor is compiled into Boot.

The verified recovery behavior includes signature rejection, incomplete
upload reset, trial health confirmation, and rollback when trial health is
missing. A polling 8-bit Boot SPI path must use
`SPI_FIFO_THRESHOLD_01DATA`; a deeper FIFO lets the STM32H7 HAL mix byte and
word RX stores and can trigger an unaligned HardFault.

## Expected output

After reset, open the ST-Link virtual COM port at 115200 baud. Startup prints
`hello`, performs one 100 ms buzzer self-test, verifies the external flash
images, scans Wi-Fi through AP6212, and finally reports that Modbus RTU is
ready. There is no periodic UART heartbeat output; the blue LED continues to
blink every 500 ms as the board-alive indicator.
