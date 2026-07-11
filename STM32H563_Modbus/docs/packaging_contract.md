# 工程与发布打包约束

## 单一源码、四个目标

本工程不再维护裸机/RTOS 两份复制源码。四个 Keil 目标共享同一份 `Core`、
`user/bsp`、`user/ldc`、`user/transport`、`user/app` 和 `Middlewares/ld_modbus`：

| 目标 | 运行环境 | USART2 RX/TX |
| --- | --- | --- |
| `STM32H563_Modbus` | 裸机 | IT / polling |
| `STM32H563_Modbus_DMA` | 裸机 | DMA / DMA |
| `STM32H563_Modbus_ThreadX_IT` | ThreadX | IT / polling |
| `STM32H563_Modbus_ThreadX_DMA` | ThreadX | DMA / DMA |

目标差异只能由 `target_config.h`、OSAL 实现、启动入口和 ThreadX middleware
表达。协议层不得包含 HAL、ThreadX、UART 或 socket 类型。

## 静态内存与边界

- 协议表、ADU、LDC 队列、UART 环形缓冲、DMA 缓冲和线程栈全部由调用方静态持有。
- ISR 只投递字节/事件，不做协议解析、AT 命令或 socket 操作。
- RTU、W800 网络服务各自拥有寄存器表；ThreadX 下不共享可变协议表。
- `check_project.ps1` 扫描工程和 `ld_modbus`，禁止运行时堆分配。
- 凭据只能放在被 Git 忽略的 `modbus_network_config_local.h`。

## 独立库发布

`Middlewares/ld_modbus` 必须能单独作为仓库根目录使用，包含 Apache-2.0
许可证、CMake、主机测试、CI、贡献和安全说明。STM32 BSP、LDC、W800、Keil
工程和硬件测试不进入独立协议库；它们保留在 STM32 集成仓库作为参考实现与实机证据。

发布前必须同时通过：

1. 独立库 CMake + CTest。
2. 四个 Keil 目标 0 error / 0 warning。
3. `check_project.ps1` 与 T3.5 主机测试。
4. 当前最终烧录目标的 COM3 RTU 从站和板端主机回归。
