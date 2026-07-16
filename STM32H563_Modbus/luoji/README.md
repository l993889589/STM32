# STM32H563 Modbus 裸机工程

本目录是一份可独立复制和编译的裸机工程，不引用 `../THREADX` 或工作区级 shared 源码。工程从 CubeMX 风格的 `Core / Drivers / MDK-ARM / user` 结构继续维护，最终构建不依赖 `.ioc` 或 `MX_*` 初始化。

## 可构建目标

| 目标 | USART2 RX | USART2 TX |
| --- | --- | --- |
| `stm32_h563_modbus_it` | ReceiveToIdle IT | polling |
| `stm32_h563_modbus_dma` | GPDMA1 Channel 1 | GPDMA1 Channel 2 |

两个目标都从 `0x08000000` 独立启动，复用本工程内部的 `ld_modbus`、LDC、AT、W800、transport 和 app 副本。协议表、ADU、队列、环形缓冲与 DMA 缓冲全部静态分配。

## BSP 规则

实体文件全部平铺在 `user/bsp`，Keil 中使用四个逻辑分组：

- `BSP/Common`：公共 `bsp_*` API、health、IRQ lock 和 safe stop；
- `BSP/MCU`：STM32H5 私有 `mcu_*` HAL/CMSIS 适配；
- `BSP/Board`：原理图相关 `board_*` 引脚、实例、极性与资源绑定；
- `BSP/Device`：`gd25lq128`、`ft6336`、`st7796`、`w800` 等芯片型号文件。

`system_stm32h5xx.c` 位于 `Core/System`。所有项目自有 C/H 文件必须包含 `@file`、`@brief` 和函数注释。

## 只编译验证

```powershell
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All
```

构建脚本只执行 Keil Rebuild，要求 `0 Error(s), 0 Warning(s)` 并校验镜像从 `0x08000000` 开始。HEX 生成已关闭；本轮不连接、下载、擦除、复位或运行开发板。
