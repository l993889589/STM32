# STM32H563 ld_modbus 示例工程

本目录包含两份可以独立编译的 STM32H563 Modbus RTU 示例，均随附完整 `ld_modbus` 源码，不使用动态内存，程序从内部 Flash `0x08000000` 启动。

## 工程入口

- [`STM32H563_ld_modbus_app`](STM32H563_ld_modbus_app/README.md)：自定义分层 BSP 的最小 USART2/RS485-1 从站，完整保留 LDC 源码，用于解释早期工程的依赖和接入方式。
- [`STM32H563_CubeMX_Modbus_Loopback`](STM32H563_CubeMX_Modbus_Loopback/README.md)：CubeMX/HAL 工程，默认是最小 USART2 从站，也可切换为 USART2 主站与 UART4 从站自动回环测试。

关于原工程为何使用 LDC、LDC 的用途、严格 T1.5/T3.5 framer 以及两个工程的详细阅读顺序，请查看任一工程随附的：

```text
Middlewares/ld_modbus/docs/STM32H563示例与LDC说明.md
```

## 验证结果

- 两份 Keil ArmClang 工程：`0 Error(s), 0 Warning(s)`；
- 实际 RS485 回环覆盖 9600、19200、115200；
- 18 项检查全部通过，0 项失败；
- T1.5 违规、T3.5 恢复、CRC 错误、错误从站地址和寄存器读写均已验证；
- UART 错误与接收溢出均为 0。

协议库的独立仓库：[l993889589/ld_modbus](https://github.com/l993889589/ld_modbus)。
