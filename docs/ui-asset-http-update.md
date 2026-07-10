# UI Asset HTTP Update Plan

## Current Scope

The desktop assistant now owns both the MQTT control plane and the HTTP asset data plane.

- MQTT broker: `0.0.0.0:1883`
- MQTT command topic: `leduo/w800/cmd`
- HTTP asset server: `0.0.0.0:8088`
- Asset metadata: `GET /ui_assets.json`
- Asset binary: `GET /ui_assets.bin`
- Default asset file: `D:\Embedded\H5\build\ui_assets\ui_assets.bin`

The board stores LVGL page images in the GD25LQ128 external flash using two 5 MB slots. The HTTP update command tells the board which HTTP URL to download and which version, size, and CRC32 to expect.

## Flash Layout

External flash is split so the bootloader download area is not reused for UI images. UI image resources use A/B slots:

| Area | Purpose |
| --- | --- |
| Slot A | Active or standby UI asset package |
| Slot B | Standby or next UI asset package |
| Reserved/test sector | Flash probe and write/verify testing |

The UI asset package contains five fixed-size RGB565 pages:

- Brand/boot page
- Monitor page
- Sensor page
- Event page
- Communication page

Each page is `480 x 320 x 2` bytes. The current full package is `1,560,576` bytes, version `2026070601`.

## MQTT Command

The desktop assistant publishes:

```json
{
  "cmd": "ui_http_update",
  "host": "192.168.1.4",
  "port": 8088,
  "path": "/ui_assets.bin",
  "version": 2026070601,
  "size": 1560576,
  "crc32": 3592326121
}
```

The board receives the command over MQTT, opens a separate W800 TCP socket to
the HTTP server, downloads the asset with HTTP Range requests, writes it to the
inactive external flash slot, validates CRC, and commits the slot. MQTT remains
connected as the command/status plane; ordinary config/data publishes are
deferred while HTTP is active.

## NetX Duo Decision

`STM32H563_App\Middlewares\ST\netxduo` exists, but the current app does not have a configured NetX Duo IP instance, packet pool, DHCP/static IP setup, or a W800 NetX link driver. W800 is currently used as an AT-command network modem, not as an Ethernet/Wi-Fi MAC owned by NetX.

Because of that, NetX Duo is not used for this update path yet. The current implementation uses raw HTTP/1.1 over the existing W800 AT TCP socket.

## Validation Results

Validated:

- Desktop release starts MQTT broker and HTTP asset server.
- `http://192.168.1.4:8088/ui_assets.json` returns package metadata.
- `http://192.168.1.4:8088/ui_assets.bin` serves the full package.
- Board connects to MQTT and publishes status.
- Board receives `ui_http_update` and opens an HTTP TCP connection to the PC.
- Board recovers MQTT after reset and normal boot.

Additional validation on 2026-07-07:

- MQTT downlink chunk transfer was tested with small chunks and is not reliable enough as a data plane on the current W800 AT path.
- A second raw TCP data socket on port `8090` was added to the desktop assistant.
- The desktop assistant can publish `ui_tcp_update` over `leduo/w800/cmd`.
- The board opens a second W800 socket to `192.168.1.4:8090` while the MQTT socket stays established.
- The desktop assistant logs `asset-tcp-client` and sends the full `1,560,576` byte asset package.
- The firmware was changed to avoid feeding image bytes into the generic AT line parser during active asset download.
- The firmware build completed with `0 Error(s), 0 Warning(s)` and was flashed through `STM32H563_App\flash_cmsis_dap.ps1`.

Additional bounded TCP chunk validation on 2026-07-07:

- The desktop assistant `8090` TCP server was changed from immediate full-package streaming to a request/response chunk protocol.
- The new MQTT control command is `ui_tcp_chunk_update`.
- The board opens a second W800 TCP socket to `192.168.1.4:8090` and sends a request frame:

```text
UIREQ <version> <offset> <length> <seq>\n
```

- The PC responds with one bounded frame:

```text
UIBLK <version> <offset> <length> <seq> <payload_crc32>\n<payload bytes>
```

- The desktop release was rebuilt so the packaged `release\win-unpacked\LeduO MQTT Server.exe` uses the new protocol.
- A desktop crash was fixed: the first implementation kept a legacy one-second full-stream fallback, so W800 could send `UIREQ` after the socket had already been ended, causing Electron main-process `ERR_STREAM_WRITE_AFTER_END`. The fallback was removed and socket writes are now guarded.
- In field testing, the board successfully connected to `8090`, the PC received `UIREQ`, and the PC sent the first `1024` byte chunk at `offset=0`.
- The board then closed the second TCP socket with `ECONNRESET`, while the MQTT socket recovered/continued publishing normal config/data traffic.
- This confirms that the second socket no longer blocks MQTT, but the firmware-side W800 `SKRCV` binary frame parser still needs exact AT receive-format capture before the full image update can complete.

Observed limitation:

- Full `1.56 MB` HTTP download through W800 AT at `115200` baud is not industrial-grade.
- Raw `SKRCV` without checking socket `rx_data` can lock or pollute the W800
  command channel; firmware now queries `AT+SKSTT=<socket>` before `SKRCV`.
- Feeding large binary socket payloads into the generic AT line parser risks line overflow and response pollution.
- At `115200`, full image transfer is slow even before flash-write and AT overhead are counted.
- The second raw TCP socket proves that the network path is possible, but the current firmware still does not complete the full image update and return to periodic MQTT publishing after the transfer. Treat this as a receiver state-machine problem, not as an MQTT reconnect problem.
- The bounded chunk protocol improves failure isolation: a failed image transfer now drops the `8090` data socket instead of taking down the main MQTT control socket.

## Industrial Recommendation

Keep MQTT as command/status only and HTTP Range as the image data plane. Do not
mix MQTT base64 chunks into normal HTTP recovery unless it is explicitly invoked
as a separate service mode. The current production boundary is:

1. Use HTTP Range blocks with `X-Range-CRC32`.
2. Query W800 `SKSTT` before every `SKRCV`.
3. Shrink failed Range blocks from 1024 to 256, 128, then 64 bytes.
4. Fail with `range retry` after the bounded retry limit; never commit a partial slot.
5. Publish MQTT ping/status during long HTTP updates, but defer ordinary config/data.
6. Add resumable package metadata:
   - package version
   - total size
   - page count
   - per-page CRC32
   - whole-package CRC32
   - active slot and pending slot
7. Add a boot-safe commit policy:
   - write inactive slot only
   - verify slot header/table/page CRCs
   - mark valid only after full verification
   - never erase the previous valid slot until the new slot is committed

This keeps memory bounded, makes retries deterministic, and prevents the W800 AT
parser from treating HTTP/MQTT payload bytes as command responses.

## Operational Commands

Build app firmware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1 -BuildOnly -Build
```

Flash app firmware:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1
```

Start desktop assistant:

```powershell
D:\Embedded\备份\desktop-debug-assistant\release\win-unpacked\LeduO MQTT Server.exe
```

Check HTTP metadata:

```powershell
Invoke-WebRequest http://127.0.0.1:8088/ui_assets.json -UseBasicParsing
```

Check active sockets:

```powershell
Get-NetTCPConnection -LocalPort 1883,8088,8090
```
