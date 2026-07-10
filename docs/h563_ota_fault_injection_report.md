# STM32H563 OTA Fault Injection Report

## Automated Matrix

| Fault | Expected invariant | Result |
| --- | --- | --- |
| Metadata write interrupted at each of 348 bytes | Previous copy remains readable | Pass |
| One metadata copy unreadable | Newest valid second copy selected | Pass |
| Firmware data omitted or out of order | Candidate never becomes pending | Pass |
| Block or whole-image CRC mismatch | Failed bytes never become active | Pass |
| Whole-image SHA-256 mismatch | Candidate rejected before Boot install | Pass |
| Install reset while state is INSTALLING | Boot retries deterministic install | Pass |
| Candidate internal programming failure | Confirmed active image restored | Pass |
| Trial fails health for three boots | Confirmed slot is reinstalled | Pass |
| Signed metadata changed | Signature verification fails | Pass |
| Signed image byte changed | CRC/SHA/signature verification fails | Pass |
| Image version below floor | Anti-rollback policy rejects candidate | Pass |
| CDC frames fragmented/coalesced | Boot recovery reconstructs by length | Pass |
| HTTP Range returns one 503 | Offset is retried; 4096-byte CRC matches | Pass |

Host commands:

```powershell
.\tools\test_ota_boot_control.ps1
.\tools\test_ota_firmware_update.ps1
.\tools\test_ota_boot_v2.ps1
.\tools\test_app_health.ps1
.\tools\test_boot_usb_recovery.ps1
.\tools\test_ota_security_policy.ps1
.\tools\test_ota_uecc.ps1
.\tools\test_ota_signing.ps1
.\tools\test_desktop_firmware_http.ps1
```

## Hardware Acceptance

The connected H563 board completed the following acceptance sequence on July
10, 2026:

| Package | Path | Observed result |
| --- | --- | --- |
| `2026071022` | HTTP Range with one injected 503 | Same offset retried, full CRC/SHA passed, slot A confirmed |
| `2026071023` | HTTP Range | `firmware-ready`, automatic reset, slot B TRIAL then CONFIRMED |
| `2026071022` | Signed downgrade | Boot error 14, slot B and minimum `2026071023` retained |
| `2026071024` | Final HTTP Range | Automatic reset, slot A TRIAL then CONFIRMED |
| `2026071023` | Final-App downgrade | `version rollback`, zero bytes received, zero new HTTP Range requests |

The final control record was `state=1`, `active=0`, `pending=255`,
`min=2026071024`, `error=0`; required health bits were `0x7` with no stale or
fault bits. Display startup alone was not used as acceptance evidence.

Option-byte destructive tests are excluded from the development board. WRP is
verified by readback in the factory procedure after all recovery paths pass.

Closing the desktop assistant window was also injected during server testing.
The rebuilt release kept ports `1883`, `8088` and `8090` listening, and a second
launch recreated the control window without restarting device services.
