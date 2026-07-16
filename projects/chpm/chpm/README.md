# CHPM STM32F401 ThreadX/USBX firmware

本目录是从只读旧工程 `F401CCU60620` 迁出的独立目标工程。目标器件为 STM32F401CCU6（256 KiB Flash、64 KiB SRAM），RTOS 使用 ThreadX，USB 使用 USBX CDC ACM。LDC 2.0.2 仅用于 DWIN 私有 UART 字节流；Modbus RTU 使用 ld_modbus 0.2.0 的 `ld_modbus_rtu_framer` 严格按字节时间戳分帧并直接交给 server，不再经过通用 LDC。

当前离线状态：ARMClang 全量链接已达到 0 error / 0 warning；主机端 Modbus/LDC 与参数掉电恢复测试通过。没有连接开发板，也没有执行下载、烧录或调试探针操作。

## 快速入口

- Keil 工程：`MDK-ARM/F4.uvprojx`
- 只编译：`powershell -ExecutionPolicy Bypass -File scripts/build_keil.ps1 -Rebuild`
- 主机测试：`powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1`
- 静态验证：`powershell -ExecutionPolicy Bypass -File scripts/validate_project.ps1`
- 板级资源：`docs/BOARD_MANIFEST.md`
- 迁移差异：`docs/MIGRATION.md`
- 线程和数据流：`docs/RUNTIME_DATAFLOW.md`
- Flash 布局：`docs/STORAGE_LAYOUT.md`
- 已验证和实机待测：`docs/BUILD_AND_VALIDATION.md`

根目录不再保留可重新生成旧 FreeRTOS/USB Device 代码的 `.ioc`。原 `.ioc` 仅作为历史证据放在 `docs/reference/F4_legacy_freertos_usb_device.ioc`，不要用它覆盖当前工程。
