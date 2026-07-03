# Migration Notes

## Background

These STM32 projects were previously maintained locally, with part of the STM32H563 work tracked on Gitee:

- `https://gitee.com/wang-kaihua/stm32-h563.git`

The GitHub repository is intended to make the work easier to review publicly and to continue open-source maintenance in a cleaner structure.

## Why This Is Not a Raw Dump

The original `D:\Embedded` workspace contained a mix of:

- active STM32 projects
- generated Keil/CubeMX output
- build products
- backups
- downloaded tools
- manuals and datasheets
- learning/reference packages

Only the source-oriented parts were copied into this public staging tree.

## History Limitation

The existing Gitee repository has commit history for the STM32H563 work. This staged GitHub tree does not preserve that full history because it reorganizes several independent folders into one public repository.

For stronger historical continuity, the recommended follow-up is one of:

1. Mirror the original Gitee repository directly to GitHub for the H5 project.
2. Keep this curated monorepo and link back to the Gitee history in the README.
3. Import the H5 project with `git subtree` or a history-preserving migration if full history is required.

## Public Release Decision

Excluded from this first publication pass:

- `D:\Embedded\备份`
- `D:\Embedded\手册`
- `D:\Embedded\资料`
- large learning/reference packages under `F4`, `H7`, `wds`, and `cs`
- generated binaries and local tooling bundles

These can be added later only after license and privacy review.
