# STM32H563 Industrial OTA Execution Record

This file is the persistent checklist for the long-running OTA goal. A stage is
complete only when its stated tests and evidence are recorded here.

## Stage Status

| Stage | Status | Acceptance evidence |
| ---: | --- | --- |
| 0. Freeze baseline | Complete | Hashes and read-only COM4 board snapshot captured |
| 1. Freeze memory architecture | Complete | Shared layout; App and Boot build with 0 errors/warnings |
| 2. Power-safe metadata | Complete | 348 torn-write offsets; one-copy read failure; dual ARM builds |
| 3. Persistent firmware A/B | Complete | Incomplete/corrupt candidate preserves active slot; LDOT v2 |
| 4. Boot install and rollback | Complete | INSTALLING retry, 3-attempt trial rollback, v1 migration tests |
| 5. App download and health confirmation | Complete | 4 KiB Range, block/whole CRC, SHA-256 and health-gated confirmation |
| 6. Independent USB recovery | Complete | Boot LDOT v2 parser is binary-safe and host fragmentation-tested |
| 7. Signature and anti-rollback | Complete | P-256 package, Boot PKA verifier, tamper/downgrade tests, WRP policy |
| 8. Diagnostics | Complete | Boot/App shell and MQTT expose slot, health, reset and control errors |
| 9. Fault injection | In progress | Host matrix complete; final board install/trial observation pending |
| 10. Production release | In progress | Reproducible release tool/runbook added; final GitHub push pending |

## Current Decisions

- Internal dual execution slots are rejected because the current 834.49 KiB App
  leaves insufficient growth margin in equal 960 KiB slots.
- The internal App remains linked at `0x08020000`.
- External firmware A/B slots are each 2 MiB.
- MQTT carries commands and low-rate status; HTTP Range carries firmware data.
- Bootloader network update and delta firmware are outside the first scope.
- New project-owned files and public functions require purpose and usage comments.

## Verified Build And Test Evidence

- Boot: Code 93402, RO 2910, RW 120, ZI 40136; 0 errors, 0 warnings.
- App: Code 461064, RO 400100, RW 2048, ZI 385152; 0 errors, 0 warnings.
- Metadata: every one of 348 interrupted write positions preserves a valid copy.
- Firmware transaction: incomplete, out-of-order, CRC-failed and SHA-failed images
  never become pending or overwrite the active slot.
- HTTP: signed manifest, 4096-byte Range CRC, injected 503 and retry all pass.
- Signature: valid package passes; changed signed metadata and changed image fail.
- Boot policy: interrupted install retries, invalid candidate restores active image,
  and an unconfirmed trial rolls back after three boot attempts.

## Commit Policy

- Work only on `codex/h563-industrial-ota-ab` for this goal.
- Stage files by explicit allow-list because the worktree contains unrelated
  historical modifications and generated dependencies.
- Publish STM32 changes to GitHub `https://github.com/l993889589/STM32.git` only.
- Do not stage Keil output, local Python runtimes, PDFs, logs, credentials, or
  device identifiers.
