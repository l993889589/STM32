# STM32H563 USB OTA And Bootloader Flow

## Purpose

This document records the current USB update path for the H563 board. It is
intended as the standard workflow after the bootloader has been provisioned once.

The USB application exposes two logical paths:

- CDC ACM: human shell, status text, and the compatibility LDOT OTA stream.
- Vendor Bulk: LDV1 framed binary channels for OTA, LDC stress, and structured commands.

The bootloader exposes CDC maintenance shell and a binary-safe LDOT v2 recovery
receiver. It does not implement Bulk OTA and it never updates its own Boot area.

## Current Protocols

### LDOT OTA Over CDC

The application accepts binary LDOT frames on CDC before shell parsing. The frame
payload limit is 224 bytes.

Current firmware A/B commands are `32` begin, `33` data, `34` finish, `35`
abort and `5` reset. The begin payload is the 124-byte signed firmware
descriptor. Commands `1..4` remain only in App as a one-time legacy migration
path; Boot recovery intentionally accepts only v2.

The PC waits for text acknowledgements:

```text
ota ack <cmd> <seq> <status>
```

### LDV1 Vendor Bulk

The application also has an LDV1 frame parser:

```text
magic "LDV1"
channel
flags
sequence
payload_length
payload_crc32
payload
```

Channels:

- `0` control
- `1` LDC
- `2` OTA
- `3` stress
- `4` log

The source tree contains an app-side DPUMP/Bulk implementation guarded by
`APP_USB_VENDOR_BULK_ENABLE` in `USBX/App/app_usbx_device.c`. It is currently
kept disabled (`0`) because enabling it caused the application USB startup to
stop enumerating reliably during bring-up. CDC OTA is the verified path.

## Flash Layout

Internal flash:

- Bootloader: `0x08000000`, size `0x00020000`
- Application: `0x08020000`, size up to the rest of internal flash

External GD25LQ128:

- Manifest A: `0x00000000`
- Manifest B: `0x00001000`
- OTA download slot: `0x00010000`, size `0x00200000`
- Rollback slot: `0x00210000`, size `0x00200000`
- UI asset slot A: `0x00500000`, size `0x00500000`
- UI asset slot B: `0x00A00000`, size `0x00500000`

The USB OTA application script writes the application image to the download slot,
writes both manifest copies, then resets the MCU. The bootloader validates the
manifest and image CRC before programming internal flash.

## Standard Application Update Command

After the fixed bootloader is on the board, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\flash_app_usb_ota.ps1" -Port COM4
```

Useful options:

- `-NoBuild`: reuse the existing Keil build output.
- `-NoReset`: upload image and manifests but do not reset into install.
- `-ImageVersion 20260709`: override the manifest image version.
- `-InputFile <path>`: package a specific `.axf`, `.elf`, `.hex`, or `.bin`.

This script does not erase or write the bootloader range.

## Bootloader Provisioning Boundary

The bootloader itself cannot be updated by either USB OTA path because:

- Bootloader USB recovery writes only the inactive external firmware slot.
- A bootloader must not overwrite its own executing flash range without a
  separate protected two-stage design.

Therefore a bootloader code change requires one of these one-time provisioning
methods:

- SWD with a known-good programmer script.
- STM32CubeProgrammer with a real ST-LINK compatible probe.
- H7-TOOL GUI/programmer flow, if configured for the target MCU.
- STM32 ROM DFU/USART bootloader, if BOOT0 and board wiring expose it.
- Factory fixture that programs only `0x08000000..0x0801FFFF`.

Once the fixed bootloader is provisioned, normal application and UI updates can
use USB/network paths without touching the bootloader area.

## 2026-07-09 Findings

- CDC OTA upload to `COM4` completed successfully for an 888608 byte app image.
- The generated manifest used image version `20260709`, package address
  `0x00010000`, image CRC `0xFEE1E07D`.
- Reset returned only CDC enumeration, not CDC plus vendor Bulk. This indicates
  the board is still running an older bootloader/application pair or Windows has
  not yet bound the new vendor interface.
- Source note: app USBX has guarded `CLASS_TYPE_VENDOR`/DPUMP code, but the
  production build keeps `APP_USB_VENDOR_BULK_ENABLE=0` until Bulk enumeration
  is debugged separately.
- Source fix: bootloader no longer skips `PENDING_UPDATE` just because the old
  application vector is valid.
- Source fix: app GD25LQ128 access is serialized with a ThreadX mutex so UI
  asset reads and OTA writes do not race on SPI1.
- Verified after bootloader provisioning: CDC OTA uploaded an 887776 byte app
  image, reset the board, and COM4 re-enumerated.

## Acceptance Checklist

After one-time bootloader provisioning:

1. Run `flash_app_usb_ota.ps1 -Port COM4`.
2. Confirm upload completes and the board resets.
3. Confirm the USB device re-enumerates.
4. Confirm app status reports the new firmware build.
5. Confirm vendor Bulk appears or test LDV1 control ping from a PyUSB tool.
6. Confirm OTA confirm task marks trial boot as confirmed after the configured delay.

The Boot shell commands `ota status` and `security` report control-copy state,
active/pending slots, trial count, last error, reset flags, key id, minimum
version and Boot WRP status. These are the first diagnostics to collect before
reflashing a device.
