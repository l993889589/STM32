# 裸机纯编译报告

日期：2026-07-11

## 范围

本报告记录 `luoji` 独立工程在 BSP 新命名和目录迁移后的最终清洁构建。只执行静态检查、主机测试、编译、链接、MAP 地址和尺寸检查；未连接、下载、擦除、复位或运行目标板。

## 工具链与策略

- Keil MDK，Arm Compiler 6.21；
- STM32H563RIVx，standalone 起始地址 `0x08000000`；
- HEX 生成关闭；
- HAL 时基为 SysTick；
- 工程不引用 `THREADX` 或工作区 shared 源码。

## 最终清洁构建

| Target | Code | RO | RW | ZI | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| `stm32_h563_modbus_it` | 53840 | 756 | 12 | 9788 | 0 error / 0 warning |
| `stm32_h563_modbus_dma` | 53840 | 756 | 12 | 9788 | 0 error / 0 warning |

两个 MAP 均确认 Load Region 与向量表从 `0x08000000` 开始。迁移前后尺寸完全一致。

## 其他验证

- `check_project.ps1`：通过；
- LDC 串口时序测试：通过；
- `ld_modbus` CMake/CTest：1/1 通过；
- 项目自有 BSP/Core/app/OSAL/transport 文件头与函数注释：通过；
- 未生成 HEX。

## 未验证

本报告不声明任何硬件验证。时钟、GPIO 电平、UART/RS485、DMA、W800、Modbus 现场通信和故障恢复仍以既有历史证据或后续授权硬件测试为准。
