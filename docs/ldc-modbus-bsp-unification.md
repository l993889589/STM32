# LDC、ld_modbus 与 BSP 统一接入说明

本次整理把通用字节流、Modbus 协议分帧和板级驱动的职责彻底分开，并将可复用库固定为 Git submodule。首次克隆仓库后执行：

```powershell
git submodule update --init --recursive
```

## 依赖版本

- LDC 2.0.2：`https://github.com/l993889589/ldc.git`，固定提交 `d795674`。
- ld_modbus 0.2.0：`https://github.com/l993889589/ld_modbus.git`，固定提交 `49657fd`。
- 库不动态分配内存，不绑定 HAL、RTOS 或 DWT。

## 工程入口

| 工程 | LDC 用途 | Modbus 用途 | 只编译入口 |
| --- | --- | --- | --- |
| `ARTPI` | 独立 LDC 2.0.2 回归目标 | 独立 ld_modbus 0.2.0 | Keil `art_pi_h750_threadx.uvprojx` |
| `projects/stm32h563-h5/STM32H563RIV6_app` | W800 UART/AT 流事务队列 | 双 RS485 RTU，协议时间戳分帧 | `build.ps1 -Clean` |
| `projects/chpm/chpm` | DWIN 私有串口帧队列 | RTU 分帧器直接接 server | `scripts/build_keil.ps1 -Rebuild` |

## 边界约定

LDC 处理通用 UART/AT 字节流的缓存、提交、读取和数据丢失报告。UART/DMA 出现 ORE、FE、NE、PE、重启失败或 LDC overflow 时，上层必须把流标记为不连续，丢弃当前开放帧，并让 AT/协议事务失败关闭；不能继续把缺字节的数据当完整帧解析。

Modbus RTU 不经过 LDC。串口层为每个接收字节提供同一单调计数源的时间戳，`ld_modbus_rtu_framer` 根据波特率、数据位、校验位、停止位和 `timestamp_hz` 计算 T1.5/T3.5。计数源可以是 DWT、硬件定时器或其他单调计数器，DWT 只属于具体 BSP 端口，不进入协议库。

BSP 使用逻辑端口和板级资源表隔离 STM32 外设实例。应用层不直接调用 `MX_*` 或 HAL 句柄；所有 BSP `.c/.h` 文件都有文件说明，公开/私有函数都有职责注释。

## 验证范围

本次只进行了主机测试、静态检查和 Keil/ARMClang 编译，没有连接、擦除、复位或烧录任何开发板。实机时序、串口错误注入和长时间满载通信仍应在对应板卡上单独验收。
