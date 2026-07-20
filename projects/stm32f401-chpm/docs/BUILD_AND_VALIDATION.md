# 构建与验证

## 只编译

首次构建前应初始化 Git submodule：

```powershell
git submodule update --init --recursive
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_keil.ps1 -Rebuild
```

脚本只调用 uVision 的 rebuild 选项，不包含下载、Flash、debug、reset 或探针命令。
当前工程关闭 HEX 自动生成，离线产物为 `MDK-ARM/F4/chpm.axf` 和 map 文件。

## 静态检查与主机测试

```powershell
powershell -ExecutionPolicy Bypass -File scripts\validate_project.ps1
powershell -ExecutionPolicy Bypass -File scripts\test_host.ps1
```

静态检查覆盖：

- Keil 与 VSCode 的 MCU 宏、头文件路径和工程文件存在性。
- 所有预期 `user/*.c`、LDC 和 ld_modbus 源文件进入目标。
- 旧 FreeRTOS、USB Device、通用 GPIO/TIM、OSAL 和第二级 Modbus LDC 适配未进入目标。
- app/protocol 不暴露 HAL API、外设实例、GPIO、DMA、句柄或寄存器。
- Modbus circular DMA 位置检查、DWT 时间戳和 T1.5/T3.5 推进顺序。
- USB VID/PID、字符串和端点保持兼容。
- 所有脚本不包含烧录或调试探针命令。

## 2026-07-20 离线结果

- Keil ARMClang/AC6 全量 Rebuild：`0 Error(s), 0 Warning(s)`。
- 镜像：Code 88892 B，RO-data 968 B，RW-data 120 B，ZI-data 46776 B。
- 静态验证：612 checks passed。
- 主机测试：参数存储/掉电回退、Modbus 参数策略、DWIN TX/RX、AHT20、
  DS18B20、LDC 与 ld_modbus 共 11 个 CTest 用例全部通过。
- 参数存储测试额外覆盖：运行期无整扇区扫描、双扇区写满返回 spare-not-ready、
  后台预擦失败保持待处理并可重试。
- 未调用 ST-Link、DAP-Link、J-Link、STM32CubeProgrammer，也未烧录或复位板卡。

## 仍需实机验证

- 25 MHz HSE、84/48 MHz 时钟和 ThreadX 长时间运行。
- USBX CDC 枚举、断连重连和持续大包。
- Modbus 在真实 RS485 电气层上的方向、终端、噪声和连续帧边界。
- W25Q64 JEDEC ID、擦写、读回与掉电恢复。
- PA8 风扇频率、极性和占空比。
- AHT20/DS18B20 时序、上拉和温度精度。
- PB14/PB15 的真实负载用途。
