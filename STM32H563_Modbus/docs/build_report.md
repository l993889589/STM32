# Compile-only build report

## Scope

This report records the dual-project baseline and the later bare-metal
schematic-peripheral compile-only checkpoint. No firmware was
downloaded, flashed, erased, executed, or tested on target hardware.

Date: 2026-07-10

## Toolchain

```text
Keil MDK Community
Arm Compiler for Embedded 6.21
Device: STM32H563RIVx
Flash profile: standalone, origin 0x08000000
HEX generation: disabled
```

## Build results

### stm32_h563_baremetal

```text
Program Size: Code=33224 RO-data=724 RW-data=12 ZI-data=5748
0 Error(s), 0 Warning(s)
```

The baseline above was superseded in `luoji` after the user authorized the
bare-metal-only peripheral completion pass. The final clean rebuild is:

```text
Program Size: Code=39904 RO-data=724 RW-data=12 ZI-data=6980
0 Error(s), 0 Warning(s)
```

Evidence: `luoji/MDK-ARM/build.log`

### stm32_h563_threadx

```text
Program Size: Code=39006 RO-data=726 RW-data=16 ZI-data=9648
0 Error(s), 0 Warning(s)
```

Evidence: `THREADX/MDK-ARM/build.log`

## Baseline functionality in both independent projects

- CMSIS and STM32H5 HAL 1.5.1 are local vendor snapshots.
- Board composition uses the `dshan_h563_industrial` board package.
- 25 MHz HSE to 250 MHz clock profile is implemented.
- Bare-metal HAL uses SysTick. ThreadX owns SysTick and uses its private TIM17 HAL timebase.
- ICACHE/DCACHE initialization is present.
- Status LED and early safe GPIO levels are present.
- PWM accepts hertz and duty permille and solves PSC/ARR/CCR internally.
- Four logical UART roles share one STM32H5 implementation.
- UART ReceiveToIdle events move bytes into static rings before LDC processing.
- The complete LDC core is shared by bare-metal and ThreadX.
- SPI1 and the initial GD25LQ128E JEDEC-ID driver compile in both targets.
- Bare-metal and ThreadX use different OSAL backends with the same API.
- Runtime contexts, ThreadX objects, stacks, rings, and packet pools are static.

## Added to the bare-metal project only

The following schematic-derived implementation is compiled only by `luoji`;
the ThreadX project remains frozen at the baseline above:

- safe logical controls for W800 boot/reset/wake, ST7796 reset/DC, FT6336U reset/interrupt, and read-only USB ID/CC logic;
- W800 bounded GPIO reset/boot sequencing plus the existing USART1 transport role;
- GD25LQ128 bounded read, page program, 4-KiB sector erase, and busy polling;
- SPI2 ST7796 initialization, window selection, RGB565 writes/fill, and physical-unit backlight PWM;
- I2C1 physical-bit-rate timing solver and FT6336U identity/two-point reads;
- two FDCAN roles using 25 MHz HSE, nominal/data timing solvers, classic/FD frames, static FIFO I/O, error diagnostics, and explicit non-ISR recovery;
- LSE-backed RTC calendar with backup-domain validity marker and no silent backup reset;
- USB DRD FS device-controller abstraction using HSI48/CRS, static PMA allocation, endpoints, stalls, transfers, and upper-stack event callbacks;
- no DMA, runtime heap, USB class, GUI, W800 AT/MQTT, Modbus, or file-system coupling in the BSP.

## Static architecture check

Each project-local `check_project.ps1` verifies the current project-owned firmware for:

- no `MX_*` dependency;
- no ThreadX headers in BSP, LDC, transport, or service layers;
- no HAL types at portable public boundaries;
- no runtime heap calls;
- no source or include path that escapes the project directory.
- project-owned C/H file headers and function comments;
- every project-owned C source is present in the Keil project;
- no HAL types in the portable public headers.

Result:

```text
project_check: ok
```

## Not verified

The following require an explicitly authorized hardware phase and are not
claimed by this report:

- HSE startup and measured 250 MHz clock;
- safe GPIO electrical levels and polarities;
- measured PWM frequency/duty;
- UART baud, ReceiveToIdle behavior, overflow recovery, or RS-485 traffic;
- SPI waveform or GD25LQ128E JEDEC ID;
- Cache/DMA coherency under traffic;
- fault capture, watchdog, bootloader, or reset behavior;
- ST7796 direction/color order/backlight polarity or FT6336U identity/coordinates;
- FDCAN bit timing, transceiver traffic, error injection, or bus-off recovery;
- RTC LSE startup/retention or USB enumeration/class behavior;
- W800 boot polarity/protocol, Modbus, FileX/LevelX, or OTA.
