# W800 UI Asset Update Plan

## Scope

This document describes the UI image package update path used by the STM32H563 board with W800 AT networking and GD25LQ128 external Flash.

The design goal is to update LVGL UI image assets over the LAN without touching the internal bootloader area and without reflashing the whole MCU application for every image change.

## Flash Layout

- Internal bootloader area remains reserved and must not be erased by UI updates.
- External Flash is used for UI assets.
- UI asset slots are A/B:
  - Slot A: `OTA_EXT_UI_SLOT_A_OFFSET`
  - Slot B: `OTA_EXT_UI_SLOT_B_OFFSET`
  - Slot size: `UI_ASSET_SLOT_SIZE`, currently 5 MB each.
- Metadata controls active slot selection. A new package is written to the inactive slot and committed only after package CRC passes.

## PC Side

The desktop tool `D:\Embedded\备份\desktop-debug-assistant` provides:

- MQTT broker on port `1883`.
- HTTP asset server on port `8088`.
- UI package source: `D:\Embedded\H5\build\ui_assets\ui_assets.bin`.
- Manifest endpoint: `/ui/manifest.json`.
- Asset endpoint: `/ui/ui_assets.bin`.

MQTT is treated as the control/status plane only. It may publish update commands,
progress, and keepalive/status messages, but it is not used as an automatic image
data fallback for HTTP Range transfers.

## Device Flow

1. PC publishes `ui_http_manifest_update` to `leduo/w800/cmd`.
2. Device validates version, size, CRC, asset path, and range chunk size.
3. Device starts an inactive external Flash slot update with `ui_asset_update_begin`.
4. Device downloads the asset by HTTP Range:
   - Default range block: 1024 bytes.
   - Each block is checked with `X-Range-CRC32`.
   - Failed blocks are not written.
   - Retry policy shrinks from 1024 to 256, 128, then 64 bytes.
   - Persistent 64-byte failures report `range retry`; the inactive slot is not committed.
5. During long HTTP downloads, MQTT ordinary config/data publishing is suppressed.
   The firmware keeps the broker connection alive with MQTT ping/status only.
6. When `received == size`, device verifies full package CRC and commits the inactive slot.

## W800 AT Notes

The W800 manual confirms the socket receive command is length based:

- `AT+SKRCV=<socket>,<maxsize>` returns a text header containing the actual payload size.
- The bytes after the header are raw socket payload.

Firmware parsing must therefore treat only the AT header as text and treat the payload as binary. Do not parse raw HTTP/MQTT payload as line text.

## Reliability Rules

- Never write a block before block CRC passes.
- Never commit a slot before full-package CRC passes.
- MQTT parser accepts only exact PUBLISH fixed header `0x30`, so ASCII AT text like `+OK=353` is not mistaken for MQTT data.
- During long HTTP downloads, firmware sends MQTT `PINGREQ` and a bounded status
  publish about every 45 seconds. Ordinary config/data publishes are deferred.
- HTTP and MQTT receive parsers reset stale AT capture state before socket operations.
- Firmware queries `AT+SKSTT=<socket>` before `AT+SKRCV`, so it only enters the
  W800 binary receive path when `rx_data` is nonzero.

## Known W800 Behavior

At asset offset around `287744`, earlier HTTP Range tests over the W800 AT socket
repeatedly corrupted data even when reduced to 64-byte blocks. The block CRC
caught this before Flash write, so the active slot was not polluted.

Current behavior:

- HTTP Range reaches the PC HTTP server and transfers 1024-byte blocks.
- Bad block at `287744` is detected by CRC and retried at smaller sizes.
- The automatic MQTT base64 gap fill has been removed from the HTTP path.
- If the retry limit is exceeded, the update fails with `range retry`; the old
  valid A/B asset slot remains active.
- `ui_chunk_update` remains as an explicit compatibility command, but it is not
  invoked by HTTP Range recovery.

## Validation Commands

Build and flash firmware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build
```

If pyOCD incremental load skips changed pages, explicitly erase and load the app sectors:

```powershell
& "D:\Embedded\H5\tools\python-3.12.4-embed-amd64\python.exe" -m pyocd commander `
  -t stm32h563rivx `
  --pack "C:\Users\99388\AppData\Local\Arm\Packs\.Download\Keil.STM32H5xx_DFP.1.2.0.pack" `
  -f 500000 `
  -c "reset halt" `
  -c "erase 0x08038000 2" `
  -c "erase 0x080f6000 1" `
  -c "load D:/Embedded/H5/STM32H563_App/MDK-ARM/STM32H563_Threadx_usbx_cdc_acm/STM32H563_Threadx_usbx_cdc_acm.hex" `
  -c "reset"
```

Publish update command:

```powershell
node -e "const mqtt=require('mqtt'); const c=mqtt.connect('mqtt://127.0.0.1:1883',{clientId:'ui-update-'+Date.now()}); c.on('connect',()=>{const p={cmd:'ui_http_manifest_update',host:'192.168.1.4',port:8088,manifest:'/ui/manifest.json',assetPath:'/ui/ui_assets.bin',version:2026070701,size:1560576,crc32:573994518,chunkSize:1024}; c.publish('leduo/w800/cmd',JSON.stringify(p),{},()=>setTimeout(()=>c.end(),300));});"
```

Watch logs:

```powershell
Get-Content "D:\Embedded\备份\desktop-debug-assistant\release\win-unpacked\logs\latest-events.jsonl" -Tail 120
```

Expected log markers:

- `asset-http-range` with `len:1024`.
- At bad data offsets, smaller `asset-http-range` retries.
- If retries are exhausted, board status reports `http.error:"range retry"`.

## Remaining Engineering Work

- Add a resumable update command that starts at a specified offset for faster bench testing.
- Add explicit status fields for the active transport mode: `http-range`, `commit`, `error`.
- Add a PC-side UI button that publishes the exact manifest update command from current asset metadata.
