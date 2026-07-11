# STM32H563 Modbus ThreadX 工程

本目录是一份可独立复制和编译的 ThreadX 工程，拥有自己的 `Core`、`Drivers`、`MDK-ARM`、`Middlewares`、`user` 和文档，不引用 `../luoji` 或工作区级 shared 源码。

## 可构建目标

| 目标 | USART2 RX | USART2 TX |
| --- | --- | --- |
| `stm32_h563_modbus_threadx_it` | ReceiveToIdle IT | polling |
| `stm32_h563_modbus_threadx_dma` | GPDMA1 Channel 1 | GPDMA1 Channel 2 |

两个目标都从 `0x08000000` 独立启动。ThreadX 使用 1 kHz SysTick，HAL 1 ms 时基由本工程私有的 TIM17 `mcu_hal_timebase.c` 提供。协议表、ADU、队列、UART/DMA 缓冲、ThreadX 控制块和线程栈全部静态分配。

## BSP 规则

实体文件全部平铺在 `user/bsp`，Keil 中使用四个逻辑分组：

- `BSP/Common`：公共 `bsp_*` API、health、IRQ lock 和 safe stop；
- `BSP/MCU`：STM32H5 私有 `mcu_*` HAL/CMSIS 适配；
- `BSP/Board`：原理图相关 `board_*` 引脚、实例、极性与资源绑定；
- `BSP/Device`：`gd25lq128`、`ft6336`、`st7796`、`w800` 等芯片型号文件。

`system_stm32h5xx.c` 位于 `Core/System`。ThreadX 类型只允许存在于 OSAL、入口和 RTOS integration，不能泄漏到 BSP、LDC、transport、AT 或 `ld_modbus`。所有项目自有 C/H 文件必须包含 `@file`、`@brief` 和函数注释。

## 只编译验证

```powershell
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All
```

构建脚本只执行 Keil Rebuild，要求 `0 Error(s), 0 Warning(s)` 并校验镜像从 `0x08000000` 开始。HEX 生成已关闭；本轮不连接、下载、擦除、复位或运行开发板。
