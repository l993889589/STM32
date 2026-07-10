# STM32H563 OTA Baseline - 2026-07-10

## Purpose

This file freezes the known source and build evidence before the industrial OTA
A/B migration. It is an audit record, not a claim that all local changes are
committed or that every binary shown here is already running on the board.

## Git Baseline

- Repository: `D:\Embedded\H5`
- GitHub remote: `https://github.com/l993889589/STM32.git`
- Starting branch: `codex/w800-http-range-stabilization`
- Starting commit: `463f74b01e86263cf1c285f364af7745f3631a8e`
- Goal branch: `codex/h563-industrial-ota-ab`
- Worktree condition: dirty before this goal; unrelated historical changes and
  generated tools must not be reverted or included in OTA commits.

## Application Build Evidence

- Link address: `0x08020000`
- Link capacity: `0x001E0000` (1920 KiB)
- Execution-region size: `0x000D0938`
- Total ROM size: 854520 bytes (834.49 KiB)
- Embedded build ID: `Jul 10 2026 13:29:20`
- AXF timestamp: `2026-07-10T13:29:41+08:00`
- AXF SHA-256: `1D421CF03456E8142BC847086964DFBCD3B5F8FE3B8DFCBDEE89B586D8607CD5`
- MAP SHA-256: `95FA45019A79ED9799E92ABDA8AC58FC415D5A519BEB673ABEBB25E5BAF356EA`

## Bootloader Build Evidence

- Link address: `0x08000000`
- Link capacity: `0x00020000` (128 KiB)
- Execution-region size: `0x00013A58`
- Total ROM size: 80592 bytes (78.70 KiB)
- AXF timestamp: `2026-07-09T17:56:30+08:00`
- AXF SHA-256: `89063F8453F52658A252AECC6EB629B61C26A1C24D8685AE7B6C2A7DAD4655E3`
- HEX SHA-256: `20F7672104E12BA473EF9741D95D7420289479B18581735B839DE2D505CCF50F`
- MAP SHA-256: `F63E2F8186DE324798F59700071391E350C5F43A00A8DBCF2C8F26F35421E3E5`

## Existing Reliable Capabilities

- Fixed 128 KiB bootloader region and application-only programming boundary.
- External GD25LQ128 access from both bootloader and application.
- External download image verification with CRC32.
- Internal application programming followed by internal CRC32 verification.
- Version-1 manifest copies at external offsets `0x000000` and `0x001000`.
- USB CDC application OTA path and HTTP Range UI asset path.

## Known Baseline Gaps

- External firmware regions behave as transient download and rollback storage,
  not durable firmware A/B slots.
- Manifest selection compares image version instead of a committed sequence.
- Both manifest sectors are rewritten for each transition without an atomic
  commit record.
- Trial boot rolls back only when the application vector becomes invalid.
- The application confirms a trial after a fixed delay without health gates.
- CRC32 detects corruption but does not authenticate firmware origin.
- Bootloader USB exposes maintenance CDC but no complete independent recovery
  image receiver.

## Hardware Snapshot

The connected board enumerated as USB CDC `COM4`. Read-only shell commands on
2026-07-10 returned:

```text
firmware 1.0.0, bsp 0.1.0
ota idle, received=0/0 bytes
ui page=dashboard asset=asset slot A version=2026071024
update=0/0 err=none@0x00000000
```

Version-1 firmware metadata has no safe shell decoder, durable active firmware
slot, boot-attempt counter, or reset-cause field. Those are recorded as baseline
limitations and become explicit diagnostics in stages 2 and 8. The embedded AXF
build ID is therefore build evidence rather than a value independently reported
by this version of the board shell.
