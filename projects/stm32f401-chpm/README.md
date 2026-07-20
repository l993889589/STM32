# CHPM STM32F401 ThreadX/USBX 固件

目标器件为 STM32F401CCU6，RTOS 使用 ThreadX，USB 使用 USBX CDC ACM。DWIN
使用 LDC 2.0.2；Modbus 使用完整 `ld_modbus` core/client/server 与 strict RTU
framer。当前产品只运行一个 Modbus server owner，不让 client 与 server 争用
USART1。

BSP 已按单板、单 MCU、单 RTOS 的扁平方式整理：每个模块直接持有 GPIO、时钟、
DMA、中断和 HAL 句柄，app/protocol 只看到逻辑设备与物理单位接口。

## 功能概览

- USB FS CDC ACM 接收上位机私有协议和 DWIN 转发帧。
- USART2 循环 DMA 接收 DWIN `5A A5 LEN` 帧；唯一 TX owner 负责发送。
- USART1 循环 DMA + DWT 时间戳运行严格 Modbus RTU server。
- W25Q64 使用双扇区追加日志保存参数，写入校验成功后才发布 RAM 和硬件状态。
- AHT20 和 DS18B20 使用可移植纯 C `sensors` 库，板级总线由 BSP 注入。
- 所有运行时对象、队列和协议缓冲均为静态分配。

## 获取源码

本工程通过 Git submodule 锁定 LDC 和 `ld_modbus`，克隆仓库时使用：

```powershell
git clone --recurse-submodules https://github.com/l993889589/STM32.git
```

已经克隆但没有子模块内容时执行：

```powershell
git submodule update --init --recursive
```

## 常用入口

- Keil 工程：`MDK-ARM/F4.uvprojx`
- VS Code 工作区：`CHPM.code-workspace`
- VSCode 默认构建：`Ctrl+Shift+B`
- 只编译：`powershell -ExecutionPolicy Bypass -File scripts/build_keil.ps1 -Rebuild`
- 静态验证：`powershell -ExecutionPolicy Bypass -File scripts/validate_project.ps1`
- 主机测试：`powershell -ExecutionPolicy Bypass -File scripts/test_host.ps1`
- 板级资源：`docs/BOARD_MANIFEST.md`
- 构建结果与实机待测：`docs/BUILD_AND_VALIDATION.md`

## 文档

- `docs/README.md`：全部说明文档索引及当前/历史状态。
- `docs/RUNTIME_DATAFLOW.md`：ThreadX 线程、IRQ、通信和传感器调度。
- `docs/USB_CDC_DWIN_PROTOCOL_AND_RISK_REVIEW.md`：上位机、USB、DWIN 协议归档。
- `docs/STORAGE_LAYOUT.md`：W25Q64 参数日志与掉电恢复。
- `docs/MIGRATION.md`：旧工程到当前 ThreadX/USBX/LDC/ld_modbus 结构的迁移。

## 已验证范围

- Keil ARMClang/AC6 全量 Rebuild：0 Error，0 Warning。
- 静态工程检查：612 项通过。
- 主机端 CTest：11/11 通过。
- 本轮只编译和测试，没有烧录、复位或连接调试探针。

硬件时钟、USB/DWIN 压力、RS485 电气层、W25Q64 掉电场景和传感器精度仍需
在目标板上验收。
