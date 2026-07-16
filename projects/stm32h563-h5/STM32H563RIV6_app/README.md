# stm32h563_app_modbus

这是 STM32H563 + ThreadX 产品 App 工程。工程保留熟悉的 CubeMX/Keil 外观，CubeMX 仅作为时钟规划种子；GPIO、UART、DMA、SPI、I2C、PWM、FDCAN、RTC、USB 等外设由 `user/bsp` 接管，应用不再调用 `MX_*` 外设初始化。

## Boot/App 边界

- Boot：`0x08000000-0x0801FFFF`。
- App：`0x08020000-0x081FFFFF`，向量表偏移 `0x00020000`。
- App 接收签名包并写入 GD25LQ128 非活动固件槽；Boot 独立执行 CRC32、SHA-256、ECDSA P-256、版本下限校验、安装、试运行和回滚。
- 现场更新使用 MQTT 命令 + W800 HTTP Range 数据面；原始 App CDC 二进制 OTA 不是发布路径。

## BSP 与应用能力

- `user/bsp`：可移植 MCU、板级和器件驱动，板级资源以 `board_resources.h` 为准。
- `user/osal`：ThreadX 适配边界，BSP 不直接依赖业务线程。
- `Middlewares/ldc`：W800 等通用 UART/AT 字节流的事务式帧队列；接收断裂会失败关闭并通知 AT 会话。
- `Middlewares/ld_modbus`：独立的 Modbus RTU/TCP 主从协议栈。RTU 使用协议自己的时间戳分帧器，不依赖 LDC，也不绑定 DWT。
- `user/app`：自动 Stop、双 FDCAN 自检/故障恢复、RTC+SPI NOR 黑匣子、量产测试、W800 MQTT 远程运维和 OTA 确认。

资源清单见 [board_resource_manifest.md](docs/board_resource_manifest.md)，最终实机证据见 [h563_final_product_acceptance_20260712.md](../docs/h563_final_product_acceptance_20260712.md)。

## 编译

首次克隆后先初始化依赖：

```powershell
git submodule update --init --recursive
```

仅编译、不连接或烧录硬件：

```powershell
.\build.ps1 -Clean
```

发布前必须同时满足：Keil `0 Error(s), 0 Warning(s)`、签名正向/篡改测试通过、Boot/App 地址不重叠、最终 AXF 导出的 BIN 哈希与 manifest 一致。

## 当前实机基线

- 签名版本：`2026071204`。
- Boot 控制记录：`CONFIRMED`，active slot B，pending none，minimum version `2026071204`，error `0`。
- 全板自检与量产测试：15/15。
- 双 RS485、双 FDCAN、LCD/触摸、PWM、SPI NOR、RTC、USB CDC、W800 均已实机通过。
