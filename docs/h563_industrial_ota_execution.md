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
| 5. App download and health confirmation | In progress | HTTP Range compiled; live server/board test pending |
| 6. Independent USB recovery | Pending | Recovery from erased internal App |
| 7. Signature and anti-rollback | Pending | Tamper and downgrade rejection tests |
| 8. Diagnostics | Pending | Reset/update/slot/error query evidence |
| 9. Fault injection | Pending | Host and hardware matrix complete |
| 10. Production release | Pending | Factory package, runbook and GitHub release state |

## Current Decisions

- Internal dual execution slots are rejected because the current 834.49 KiB App
  leaves insufficient growth margin in equal 960 KiB slots.
- The internal App remains linked at `0x08020000`.
- External firmware A/B slots are each 2 MiB.
- MQTT carries commands and low-rate status; HTTP Range carries firmware data.
- Bootloader network update and delta firmware are outside the first scope.
- New project-owned files and public functions require purpose and usage comments.

## Commit Policy

- Work only on `codex/h563-industrial-ota-ab` for this goal.
- Stage files by explicit allow-list because the worktree contains unrelated
  historical modifications and generated dependencies.
- Publish STM32 changes to GitHub `https://github.com/l993889589/STM32.git` only.
- Do not stage Keil output, local Python runtimes, PDFs, logs, credentials, or
  device identifiers.
