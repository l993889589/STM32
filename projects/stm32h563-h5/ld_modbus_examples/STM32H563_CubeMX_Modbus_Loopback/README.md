# STM32H563 CubeMX ld_modbus 示例

关于早期工程为什么依赖 LDC、LDC 的用途以及两份 STM32H563 示例的完整入口说明，请阅读 `Middlewares/ld_modbus/docs/STM32H563示例与LDC说明.md`。

这是一个可直接编译、烧录的 STM32H563 Modbus RTU 独立 App 工程。工程由 STM32CubeMX 生成底层初始化代码，程序从内部 Flash `0x08000000` 启动，不依赖 Bootloader、RTOS、LDC 或动态内存。

默认编译的是一个便于移植的 USART2 Modbus RTU 从站。较大的双串口自动测试代码单独放在 `Tests` 目录，不会妨碍只想参考最小从站的使用者。

## 工程结构

```text
App/                         示例选择与最小 USART2 从站
Tests/                       USART2 主站 + UART4 从站自动回环测试
Middlewares/ld_modbus/       完整 ld_modbus 源码副本
Core/、Drivers/、MDK-ARM/    CubeMX 生成的 HAL 工程
STM32H563_*.ioc              CubeMX 配置文件
```

`ld_modbus` 核心处理 CRC、功能码、寄存器映射和请求/响应；可选的 `ld_modbus_rtu_framer` 处理 T1.5/T3.5 组帧。UART、RS485 收发方向和 TIM2 仍由平台工程负责，协议库本身不依赖 HAL、定时器、操作系统或堆内存。

## 默认：最小 RTU 从站

配置文件为 `App/modbus_example_config.h`：

```c
#define MODBUS_EXAMPLE_MODE MODBUS_EXAMPLE_MODE_SLAVE
```

默认参数：

- USART2，115200，8N1，从站地址 `1`；
- TIM2 为 1 MHz、32 位自由运行计数器；
- UART 每字节中断接收，回调只保存字节和“字符接收完成”时间戳；
- 主循环调用 `ld_modbus_rtu_framer` 完成严格 T1.5/T3.5 判断，再交给 `ld_modbus`；
- 全部缓冲区和寄存器表静态分配。

保持寄存器 `0..7` 依次为：固定标识 `0x0563`、接收帧数、发送帧数、CRC 错误数、协议错误数、T1.5 违规数、T1.5 微秒值、T3.5 微秒值。调试器可观察 `g_modbus_slave_report`。

## 可选：双串口自动回环测试

将 `App/modbus_example_config.h` 改为：

```c
#define MODBUS_EXAMPLE_MODE MODBUS_EXAMPLE_MODE_LOOPBACK
```

测试模式使用 USART2 作为 RTU 主站、UART4 作为 RTU 从站，两路 RS485 需要物理互连。测试覆盖 9600、19200、115200 三种波特率，以及读写寄存器、CRC 错误、错误从站地址、T1.5 违规丢弃和 T3.5 后恢复。

调试器观察 `g_modbus_loopback_report`：`state == MODBUS_LOOPBACK_STATE_PASS`、`last_error == 0`、`failed_checks == 0` 表示通过。

本工程已在实际 STM32H563 板卡上验证：18 项检查全部通过，3 种波特率全部完成，检测到 3 次预期的 T1.5 违规，UART 错误和接收溢出均为 0。

## T1.5/T3.5 的边界

`ld_modbus_rtu_framer` 接收由驱动提供的字节及其接收完成时间戳：

- 波特率不高于 19200 时，根据波特率和每字符位数向上取整计算；
- 波特率高于 19200 时，使用规范建议的 `T1.5 = 750 us`、`T3.5 = 1750 us`；
- 帧内静默达到 T1.5 但未达到 T3.5 时，当前帧无效，并持续丢弃到下一次 T3.5 静默；
- 静默达到 T3.5 时，提交完整 RTU ADU。

因为时间戳记录在字符接收完成时，相邻完成时间戳包含当前字符的传输时间。framer 在判断总线静默时会扣除这段字符时间，平台层不应重复补偿。

## CubeMX 外设配置

- USART2：PA2/PA3，对应板上 RS485-1；
- UART4：PA0/PA1，对应板上 RS485-2，仅自动回环测试使用；
- TIM2：1 MHz 自由运行微秒计数器；
- PC12：状态 LED；
- 板上 MAX13487 使用自动方向控制，不需要软件操作 DE 引脚。

若移植到手动 DE 的 RS485 收发器，应在发送前后由 UART/RS485 驱动控制方向，不要把 DE 控制写入 `ld_modbus`。

## 编译

在工程根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

也可直接打开 `MDK-ARM/STM32H563_CubeMX_Modbus_Loopback.uvprojx`。预期结果为 `0 Error(s), 0 Warning(s)`。

修改 `.ioc` 并重新生成代码时，请保留 CubeMX 的 USER CODE 区，以及 `App`、`Tests`、`Middlewares` 和 Keil 工程中手工加入的文件组。
