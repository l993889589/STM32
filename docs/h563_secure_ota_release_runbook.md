# STM32H563 Secure OTA Release And Recovery Runbook

## Security Boundary

Production firmware packages are signed with ECDSA P-256. The private key stays
outside the repository; Boot contains only public key id `9B6DB882D4D4D93F` and
the public coordinates. The signed canonical record contains schema, image
version, size, CRC32, flags, load address, entry address and image SHA-256.

Boot independently performs these checks before internal flash programming:

1. External slot bounds, vector range and whole-image CRC32.
2. Whole-image SHA-256 against the signed descriptor.
3. Raw P-256 `r||s` signature using the Boot-local micro-ecc secp256r1 verifier.
4. Image version against the larger of the compiled floor and confirmed floor.

The confirmed version floor blocks remote and normal software downgrade. A
physical attacker able to replace both the SPI NOR and its control records can
replay an older signed package unless a device-specific OTP monotonic counter or
secure element is provisioned. That stronger physical anti-rollback policy is a
product security decision, not silently claimed by this implementation.

STM32H563 does not contain the PKA accelerator present on some other STM32H5
parts. Do not enable the shared HAL PKA driver for this target. Boot compiles
micro-ecc in verify-only secp256r1 configuration; its BSD-2-Clause notice and
exact imported revision are recorded in `docs/THIRD_PARTY_NOTICES.md`.

## Release Package

Run from `D:\Embedded\H5`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\new_h563_factory_release.ps1 -ImageVersion 2026071018
```

The script rebuilds Boot and App, signs the App package, verifies the exact
compiled public key, copies programming artifacts, and writes SHA-256 checksums
plus `release-info.json`. The output folder is not source-controlled.

Rules:

- Never use `-SkipBuild` for a production release unless the exact Keil logs are archived.
- Never publish or copy `h563_ota_private.pem` into the release directory.
- Never reuse an image version; release versions must increase monotonically.
- Keep Boot, App, manifest, checksums and source commit id in the factory record.

## Provisioning Order

1. Program Boot HEX at `0x08000000` and App HEX at `0x08020000` through a fixture.
2. Read back and compare programmed ranges with release SHA-256/checksums.
3. Boot the device and collect `ota status` plus `security` over CDC.
4. Exercise one signed USB recovery update before locking the unit.
5. Enable Boot WRP only with a dedicated factory build where
   `BOOT_PROTECTION_PROGRAMMING_ENABLED=1`.
6. Reconnect after option-byte reload and verify `security` reports Boot protected.
7. Apply the product RDP policy only after SWD recovery requirements are approved.

WRP/RDP programming is deliberately disabled in development builds. The
factory must use a supported ST-LINK/STM32CubeProgrammer version and archive the
option-byte readback. Do not experiment with irreversible option bytes on the
development board.

## Field Update

1. MQTT sends the signed manifest command; no firmware bytes travel in MQTT.
2. App downloads 4096-byte HTTP Ranges into the inactive external slot.
3. Every Range CRC is checked before write; the complete CRC32 and SHA-256 are
   checked before the pending slot is published atomically.
4. App publishes one `firmware-ready` status and resets automatically; Boot then
   authenticates and installs the pending slot.
5. App confirms only after 60 seconds of RS485, W800 and UI scheduler health.
6. Failed trials are retried and then automatically restore the confirmed slot.

The App performs a bandwidth-saving version-floor check before the first HTTP
request. This is not the security boundary: Boot independently checks the signed
descriptor and confirmed floor before erasing internal App flash.

## Recovery

If App is erased or cannot start, keep the device in Boot CDC and run the same
LDOT v2 signed package transfer. Boot recovery is length-framed and does not use
CR/LF delimiters. It supports fragmented and coalesced CDC packets, CRC16 per
frame, transaction abort and reset.

Collect these before changing flash:

```text
ota status
security
```

Do not erase the external control sector during diagnosis. It contains the
active/rollback decision and the last persistent OTA error.
