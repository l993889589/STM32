# H5 Workspace Layout And Cleanup Record

## Stable Layout

```text
H5/
  STM32H563_Bootloader/   secure Boot and USB recovery
  STM32H563_App/          runtime firmware and LVGL UI
  shared/                 Boot/App contracts and reviewed libraries
  desktop-debug-assistant independent Git submodule
  simulators/lvgl/        PC simulator entry and configuration
  assets/ui/              source and reference UI artwork
  tools/ and tests/       host automation and regression tests
  docs/                   architecture and operating records
  reference/              local manuals, excluded from Git
  artifacts/              local readbacks, excluded from Git
```

The firmware directories intentionally remain at the root because their Keil
projects share `shared/` through stable relative paths.

## Bootloader Reduction

The keep-set was generated from the final zero-warning Keil target file list and
all compiler dependency files. The cleanup removed 1,748 unused vendor files
(40.88 MiB), including CMSIS DSP/NN/DAP/Core-A, unused HAL modules, unused
ThreadX/USBX ports and classes. Keil target output, logs, debugger configuration,
`.uvoptx` and GUI state are generated locally and are not source.

After pruning, a build from an empty output directory produced the same image
size as before: Code 116842, RO-data 3114, RW-data 120, ZI-data 57576, with zero
errors and zero warnings.

## Desktop Migration

The source repository moved from its former backup location to
`D:\Embedded\H5\desktop-debug-assistant`. It remains an independent repository
and is referenced by the STM32 workspace as a Git submodule. Generated releases,
logs, `node_modules` and historical release copies were not migrated.
