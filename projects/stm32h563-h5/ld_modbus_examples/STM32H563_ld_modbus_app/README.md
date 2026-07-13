# STM32H563 ld_modbus 独立 App 示例

关于早期工程为什么依赖 LDC、LDC 的用途以及两份 STM32H563 示例的完整入口说明，请先阅读 `Middlewares/ld_modbus/docs/STM32H563示例与LDC说明.md`。

这是一个使用自定义分层 BSP 的 STM32H563 Modbus RTU 从站工程，程序从内部 Flash `0x08000000` 启动。它不依赖产品 Bootloader、RTOS 或动态内存，适合展示如何把 `ld_modbus` 接入个人 BSP。

如果希望使用完全由 CubeMX/HAL 生成的初始化代码，请参考同级目录 `STM32H563_CubeMX_Modbus_Loopback`。

## 默认运行路径

```text
USART2 每字节接收 + 完成时间戳
        -> ld_modbus_rtu_framer（T1.5/T3.5）
        -> 完整 RTU ADU
        -> ld_modbus 从站处理
        -> USART2 响应
```

默认参数位于 `user/app/modbus_app_config.h`：从站地址 `1`、USART2/RS485-1、115200 波特率、每字符 11 位。`bsp_uart_get_baud_rate()` 返回实际波特率，应用据此配置 framer，避免协议配置和 UART 实际配置不一致。

`ld_modbus_rtu_framer` 是 `ld_modbus` 仓库随附的可选模块，不依赖 HAL、UART、DWT、TIM、LDC 或操作系统。它只接收静态缓冲区、字节和字符接收完成时间戳；硬件取时仍由 BSP 负责。

## TIM2 与 DWT

统一时间接口为：

```c
bsp_status_t bsp_microtime_init(void);
uint32_t bsp_microtime_now_us(void);
```

在 `user/bsp/bsp_microtime_config.h` 中选择实现：

```c
#define BSP_MICROTIME_BACKEND BSP_MICROTIME_BACKEND_TIM2
```

默认使用 TIM2 作为 1 MHz、32 位自由运行计数器，不产生更新中断；也可切换为 `BSP_MICROTIME_BACKEND_DWT`。所有时间差采用无符号减法，能够处理 32 位计数回绕。TIM2 后端独占 TIM2，移植时需要根据目标时钟树确认定时器输入时钟。

## 与旧 LDC 接收路径的关系

之前 H5 产品工程的接收链路依赖 LDC：

```text
UART -> ldc_easy_add() -> ldc_easy_tick_us() -> ldc_easy_pop() -> ld_modbus
```

LDC 的静默超时能够实现 T3.5 帧结束，但它是通用数据分帧机制，没有独立的 Modbus T1.5 违规判断。这个独立示例默认设置 `MODBUS_APP_USE_LDC = 0`，改用 `ld_modbus_rtu_framer` 严格处理 T1.5 和 T3.5。

工程仍完整保留 `user/ldc` 与 `Middlewares/ld_modbus/integrations/ldc`，因此不会再出现缺少 `ldc_easy.h` 或 `ldc_easy_pop()` 实现的问题。它们用于展示旧集成方式，不是默认编译运行链路。

## 静态内存与寄存器

App、BSP、LDC 和 `ld_modbus` 均不调用堆分配。保持寄存器 `0..7` 依次为：固定标识 `0x0563`、接收帧数、发送帧数、CRC 错误数、协议错误数、T1.5 违规数、T1.5 微秒值、T3.5 微秒值。

## 编译

```powershell
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

也可直接打开 `MDK-ARM/STM32H563_ld_modbus_app.uvprojx`。TIM2 和 DWT 两种后端均已通过 Keil ArmClang 编译检查，预期为 `0 Error(s), 0 Warning(s)`。

实际双串口、多波特率和异常间隔硬件验证放在 CubeMX 工程的 `Tests` 目录中，避免把复杂测试状态机塞进这个最小从站示例。
