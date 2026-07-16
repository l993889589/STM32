# 裸机工程打包约束

`luoji` 必须能在脱离 `THREADX` 和工作区其他源码的情况下独立复制、静态检查和编译。

## 本地所有权

- 本工程独立拥有 `Core`、`Drivers`、`MDK-ARM`、`Middlewares/ld_modbus`、`user`、`tests` 和 `docs`。
- Keil 源文件路径不得包含绝对路径、`..\..\` 或指向 `THREADX` 的路径。
- 不创建编译期 shared 目录；需要同步的代码以文件副本形式评审，并分别编译。
- 裸机仅包含 `osal_bare_metal.c`，不得包含或引用 ThreadX 头文件和中间件。

## 目标

| 目标 | RX/TX |
| --- | --- |
| `stm32_h563_modbus_it` | IT / polling |
| `stm32_h563_modbus_dma` | DMA / DMA |

两目标必须使用同一份本工程内部协议、BSP、LDC、AT、transport 和 app；差异只能由 target 配置表达。

## 发布门槛

1. `check_project.ps1` 通过文件/函数注释、来源覆盖、无堆、无 MX/RTOS 泄漏检查。
2. `tests/test_timing.ps1` 通过。
3. 两个 Keil target 均为 0 error / 0 warning。
4. HEX 生成关闭，编译任务不得连接或操作目标板。
