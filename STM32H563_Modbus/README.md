# STM32H563 Modbus 工作区

本目录包含两套相互独立、可分别复制和编译的 Modbus 工程：

- `luoji/`：裸机 IT 与 DMA 两个 target；
- `THREADX/`：ThreadX IT 与 DMA 两个 target。

每套工程都独立拥有 `Core`、`Drivers`、`MDK-ARM`、BSP、LDC、`ld_modbus`、AT/W800、transport、app、测试和文档。不存在工作区级编译共享源码，也不存在从一个工程指向另一个工程的 Keil 文件路径。

## BSP 命名

两套工程的 `user/bsp` 都保持 CubeMX 熟悉的平铺结构，Keil 使用以下逻辑分组：

- `BSP/Common`：`bsp_*`；
- `BSP/MCU`：`mcu_*`；
- `BSP/Board`：`board_*`；
- `BSP/Device`：直接使用外部芯片型号。

`system_stm32h5xx.c` 放在 `Core/System`，不属于 BSP。

## 编译入口

```powershell
cd .\luoji
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All

cd ..\THREADX
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All
```

两套工程均关闭 HEX 生成。当前工作只允许静态检查、主机测试和 Keil Rebuild，不连接、下载、擦除、复位或运行开发板。
