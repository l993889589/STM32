# 构建、已验证项与实机待测

## 构建

要求 Keil MDK 5，当前验证路径为 `C:\Keil_v5`，编译器为 ARMClang/AC6。

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_keil.ps1 -Rebuild
```

脚本只调用 uVision 的 build/rebuild 选项，不包含下载、Flash、debug 或探针命令。工程关闭 HEX 自动生成，离线产物为 `MDK-ARM/F4/chpm.axf` 和 map 文件。

主机测试与静态检查：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\test_host.ps1
powershell -ExecutionPolicy Bypass -File scripts\validate_project.ps1
```

## 已验证

- ARMClang 全工程编译和链接：0 error / 0 warning。
- 镜像位于 STM32F401CCU6 的 256 KiB Flash / 64 KiB SRAM 边界内。
- `ld_modbus` core 与 strict RTU framer：MSVC Release 2/2 CTest 通过，且 `/W4 /WX`；不编译已移除的第二级 LDC adapter 集成。
- LDC 2.0.2 canonical dependency：MSVC Release CTest 通过，且 `/W4 /WX`；依赖固定为提交 `d795674`，工程不修改库源码。
- 参数存储实际固件源文件通过 mock NOR Flash 测试：空白默认值、旧 v0x101 迁移、A/B 选择、CRC/commit 损坏回退、commit 阶段写失败、运行时回滚、非法值无写入。
- 静态验证共 447 项，检查 Keil 文件存在性、目标 MCU、禁用 HEX、无旧栈编译组/包含、唯一 DWIN LDC owner、strict Modbus 数据流与 DMA 时间推进顺序、canonical 哈希、USB VID/PID/字符串/端点、PA6/PWM 冲突和脚本无下载命令。
- 未调用 ST-Link、DAP-Link、J-Link、STM32CubeProgrammer 或任何烧录动作。

## 无硬件无法验证

- 25 MHz HSE 起振、84/48 MHz 时钟实测和 ThreadX 长时间节拍漂移。
- USBX 在 Windows/Linux 的 CDC 枚举、旧驱动复用、UID 串号、断连/重连、BUSY/背压和持续大包。
- 外接 RS485 收发器的电平、DE/RE 方向、终端、偏置和总线冲突；当前板原理图只给出 TTL USART1。
- Modbus 在真实噪声、碎帧、连续帧、广播和上位机超时条件下的行为；特别要用逻辑分析仪核对 DWT 插值时间戳、DMA HT/TC/IDLE 交错及 T1.5/T3.5 边界。
- W25Q64 JEDEC ID、最坏擦除时间、读回、真实掉电注入和长期磨损。
- TIM1 PA8 风扇电气极性、25 kHz 频率、40–100% 占空比对应转速。
- AHT20、DS18B20 传感器存在性、PB0/PB1 实际装配、温度比例和报警阈值。
- DWIN 全部页面、事件文本、复位帧、ACK/异步帧路由和串口背压；验证唯一 owner 不会丢失请求完成事件。
- PB14/PB15 的真实负载；确认前不得扩展其业务。
