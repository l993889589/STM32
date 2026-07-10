# STM32H563 Industrial OTA Architecture

## Target

The target is a fixed bootloader, one internal runtime application, persistent
external firmware A/B packages, external UI A/B assets, bounded trial boot, and
an independent USB recovery path.

```text
MQTT command / USB tool
          |
          v
Application download manager --HTTP Range--> inactive external firmware slot
          |
          v
verified candidate + atomic boot-control request
          |
        reset
          |
          v
Bootloader --> verify package --> install internal App --> verify --> trial
                                                         |          |
                                                    confirmed    rollback
```

## Internal Flash

| Region | Start | Size | Owner |
| --- | ---: | ---: | --- |
| Bootloader | `0x08000000` | 128 KiB | Boot build only |
| Runtime application | `0x08020000` | 1920 KiB | Boot installer and App execution |

The bootloader never downloads through W800 and does not contain MQTT, HTTP,
LVGL, or product policy. It validates state, installs, verifies, rolls back,
provides USB recovery, and jumps to the application.

## External GD25LQ128

| Region | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| Boot control | `0x000000` | 64 KiB | Redundant records and boot diagnostics |
| Firmware A | `0x010000` | 2 MiB | Persistent verified package |
| Firmware B | `0x210000` | 2 MiB | Persistent verified package |
| Diagnostics | `0x410000` | 960 KiB | Crash/configuration records and reserve |
| UI A | `0x500000` | 5 MiB | Active or candidate UI package |
| UI B | `0xA00000` | 5 MiB | Active or candidate UI package |
| Factory reserve | `0xF00000` | 1 MiB | Factory identity and future use |

Only an inactive slot may be erased. A slot becomes eligible for installation
only after block checks and full-package verification complete.

## Ownership

| Resource | Runtime owner | Rules |
| --- | --- | --- |
| Boot-control records | Boot state machine; App submits bounded requests | No arbitrary sector writes outside the shared API |
| Firmware inactive slot | App OTA service or Boot USB recovery | Mutually exclusive writer; active slot remains immutable |
| Internal application | Bootloader | Application never erases or programs its own image |
| UI slots | UI asset service | Firmware OTA cannot erase UI ranges |
| SPI1 / GD25LQ128 | One serialized driver owner per image | No bus reinitialization by clients |

## Required State Model

```text
EMPTY -> DOWNLOADING -> VERIFIED -> PENDING -> INSTALLING -> TRIAL
                                                        |       |
                                                        |       +-> CONFIRMED
                                                        +----------> ROLLBACK
```

Every persistent transition writes a new record with a monotonically increasing
sequence, validates the complete record, then commits it. The previous committed
record remains usable until the new commit succeeds.

## Failure Rules

- Interrupted download: continue running the confirmed internal application.
- Invalid candidate: reject it without changing active firmware state.
- Power loss during installation: retry from the intact pending external slot.
- Trial reset or watchdog failure: increment a bounded attempt counter.
- Attempt limit reached: reinstall the previous confirmed external slot.
- External flash unavailable with valid internal App: boot in degraded mode and
  report diagnostics after communication becomes available.
- Invalid internal App and no valid external package: enter USB recovery.

## Trial Confirmation

Elapsed time alone is not a health signal. Confirmation requires all mandatory
services to report recent progress, required storage/configuration to be valid,
the watchdog supervisor to be active, and the system to remain healthy for a
configured observation window.

## Security Boundary

CRC32 is retained for transfer diagnostics. Production acceptance additionally
requires SHA-256, a bootloader-held public key, signed firmware manifests,
hardware compatibility checks, and a monotonic minimum-version policy. Online
bootloader self-update is outside the first production scope.
