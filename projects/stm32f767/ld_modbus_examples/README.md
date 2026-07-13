# STM32F767 ld_modbus 示例总入口

这里提供两份可以分别复制、打开和编译的 STM32F767IGT6 CubeMX/Keil 工程。两份工程都基于 Issue 提交者提供的附件移植，保留原 CubeMX/HAL 文件和使用习惯，不依赖作者个人 BSP，也不依赖目录外源码。

| 工程 | 运行方式 | 入口 | 内存策略 |
| --- | --- | --- | --- |
| 裸机版 | `while (1)` 调用 `app_poll()` | [`STM32F767_ld_modbus_baremetal`](STM32F767_ld_modbus_baremetal) | 全静态 |
| FreeRTOS 版 | 静态 Modbus 任务每 1 ms 调用 `app_poll()` | [`STM32F767_ld_modbus_freertos`](STM32F767_ld_modbus_freertos) | 静态任务、静态栈、禁用动态分配 |

## 公共功能

- USART1 PA9/PA10：115200 `printf` 调试口，保留提交者原用途；
- USART3 PB10/PB11：RS232 点对点 Modbus RTU；
- USART3 使用 `HAL_UART_Receive_IT(..., 1)` 普通单字节中断；
- 不使用 DMA、ReceiveToIdle、RS485 DE/RE、DWT 或额外硬件定时器；
- 使用标准 1 ms SysTick 与 `SysTick->VAL` 生成微秒时间戳；
- 严格处理 T1.5 帧内违规和 T3.5 帧结束；
- 支持 RTU Slave/Master 与 9600、19200、115200；
- 默认 Slave，地址 1，波特率 115200；
- `ld_modbus` 源码完整放在各工程的 `Middlewares/ld_modbus` 中；
- 不依赖 LDC，不使用堆内存。

## 四类数据表权限

Modbus 协议侧权限如下：

| 数据表 | 远程主站读取 | 远程主站写入 | 本机应用更新接口 |
| --- | --- | --- | --- |
| Coil | 支持 | 支持 | `read/write` |
| Discrete Input | 支持 | 不支持 | `read/set` |
| Holding Register | 支持 | 支持 | `read/write` |
| Input Register | 支持 | 不支持 | `read/set` |

这里的 `set_discrete_input()` 和 `set_input_register()` 只允许本机应用或传感器任务更新采集值，并不会增加任何远程写功能码。8 个应用访问接口都进行空指针和地址范围检查。

## 配置与验证

角色和波特率统一在各工程的 `app/modbus_app_config.h` 中选择。每个工程都附带：

- `check_project.ps1`：检查自包含源码、单字节中断、静态内存及禁止依赖；
- `build.ps1`：删除旧日志并执行 Keil 完整 Rebuild；
- `test_build_matrix.ps1`：验证 Slave/Master × 三种波特率共 6 个配置，并自动恢复默认配置。

ArmClang 6.21 下两份工程共 12 个矩阵配置全部为 `0 Error(s), 0 Warning(s)`。此外，`ld_modbus` 的 GCC、MinGW、严格 RTU framer 和四类数据表访问器宿主测试全部通过。

本次验证严格限定为编译、静态检查和宿主测试：没有连接探针，没有烧录、擦除、复位或运行提交者的 STM32F767 开发板。RS232 实际收发、任务调度和 SysTick 微秒插值仍需在目标硬件上验证。
