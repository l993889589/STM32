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
.\tools\test_ota_signing.ps1
.\tools\test_desktop_firmware_http.ps1
```

## Hardware Acceptance

The final board run records the signed package version, Boot `ota status`, Boot
`security`, App health status, install reset and confirmed state. A test is not
accepted merely because the display starts; the control record must show no
pending slot, the expected active slot/version and a raised minimum version.

Option-byte destructive tests are excluded from the development board. WRP is
verified by readback in the factory procedure after all recovery paths pass.
