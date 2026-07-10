# STM32H563 H5 Workspace

This workspace keeps firmware, host tools, reusable code and local artifacts in
separate ownership boundaries.

| Path | Purpose |
| --- | --- |
| `STM32H563_Bootloader/` | Minimal secure Boot, USB recovery and OTA installer |
| `STM32H563_App/` | Runtime application, communication services and LVGL UI |
| `shared/` | Boot/App shared OTA contracts and reviewed third-party code |
| `desktop-debug-assistant/` | Independent Git submodule for MQTT/HTTP host services |
| `simulators/lvgl/` | Windows LVGL simulator source; build output stays local |
| `assets/ui/` | Source and visual-reference images used to build UI packages |
| `tools/` and `tests/` | Packaging, programming and host regression tools |
| `docs/` | Architecture, acceptance records and operating runbooks |
| `reference/` | Local manuals and extracted reference text; not published |
| `artifacts/` | Local readbacks and diagnostics; not published |
| `build/`, `ota_package/`, `release/` | Reproducible generated output |

Firmware directories remain at the workspace root deliberately. Their Keil
projects and factory scripts use stable relative paths to `shared/`; moving them
under another wrapper directory would add path churn without improving ownership.
