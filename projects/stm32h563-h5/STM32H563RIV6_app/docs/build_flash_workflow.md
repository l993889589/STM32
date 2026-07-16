# STM32H563 App Build And Flash Workflow

## Purpose

This note defines the normal build/flash path for the STM32H563 application
project. The goal is to avoid repeatedly proving the programmed firmware by
manual Flash readback during everyday development.

## Daily Command

Run this from any PowerShell prompt:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build
```

This command:

1. Rebuilds the Keil app target with `UV4.exe -r`.
2. Checks the Keil build log for `0 Error(s)`.
3. Erases the full application range before programming:

```text
0x08020000 - 0x081FFFFF
```

The bootloader range `0x08000000 - 0x0801FFFF` is not touched.

4. Flashes this exact app HEX:

```text
D:\Embedded\H5\STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm\STM32H563_Threadx_usbx_cdc_acm.hex
```

5. Uses pyOCD sector erase/program/verify unless `-NoVerify` is explicitly
   passed.
6. Resets the target.

The script prints the HEX and AXF paths plus their timestamps before flashing.
That printout is the daily confirmation of which artifact is being programmed.

## What Not To Use As Proof

Do not use MQTT `fwBuildId` alone as proof that flashing failed or succeeded.
MQTT output depends on the W800 connection state and application runtime path.
An old MQTT line can remain in logs after reset, and a new firmware can be
correctly programmed while the W800 task is still reconnecting.

Do not read back internal Flash for every normal code change. It is slow and
does not help debug runtime W800/MQTT state.

## When Deep Verification Is Allowed

Use manual readback only when one of these is suspected:

- The app was flashed to the wrong address.
- The bootloader overwrote the app from external Flash.
- The debug probe selected the wrong target.
- pyOCD reported a programming or verify error.

The app project is linked at `0x08020000`, matching the bootloader layout:

```text
Bootloader: 0x08000000 - 0x0801FFFF
App       : 0x08020000 - 0x081FFFFF
```

## Runtime Validation

After the daily flash command succeeds, runtime validation should focus on the
feature under test:

- For W800/MQTT, check broker client/connect/subscribe/publish behavior.
- For HTTP asset update, check `http.pending`, `http.active`, `http.received`,
  and `asset.error`.
- For display work, check the panel output and LVGL state.

If runtime validation fails but pyOCD completed successfully, treat it as a
runtime bug first, not a flashing bug.

## 2026-07-09 Confirmation

The command above was updated after a real flashing pitfall was found: pyOCD's
normal HEX sector erase can leave stale bytes in the application area when code
or constants move. This once left an old MQTT client ID string in internal
Flash. The script now erases the complete application range first, then flashes
the HEX.

The final accepted build printed:

```text
HEX/AXF time  : 2026-07-09 17:34
fwBuildId     : Jul  9 2026 17:34:10
clientId      : leduo-h563-w800
deviceId      : leduo-h563-w800
asset.version : 2026070701
asset.error   : none
```

One-time readback confirmed the application Flash no longer contained
`leduo-h563-w800-codex`. Routine readback is still not required because the
script now erases the whole app range before every program operation.

## W800 Reset Note

The W800 module keeps its own power and socket state across an MCU-only reset.
`bsp_w800_hard_reset()` depends on `PC9` being configured as a push-pull output.
If `PC9` is not initialized, W800 can keep an old TCP/MQTT session alive even
after the MCU has been rebuilt and flashed. Runtime validation should therefore
look for a fresh disconnect/connect cycle and the current `fwBuildId`.
