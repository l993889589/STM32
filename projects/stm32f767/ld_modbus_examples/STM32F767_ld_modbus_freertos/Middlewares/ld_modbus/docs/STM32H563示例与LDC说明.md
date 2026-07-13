# STM32H563 示例工程与 LDC 说明

## 写在前面

早期公开的 STM32H563 Modbus 示例并不是一份完全独立的 `ld_modbus` 示例。当时工程的串口接收、静默计时和完整帧提取使用了作者自己的 **LDC** 库，典型链路如下：

```text
UART 接收回调
    -> ldc_easy_add()
    -> ldc_easy_tick_us() 累计静默时间
    -> ldc_easy_pop() 取出完整数据帧
    -> ld_modbus 解析请求并生成响应
```

因此，只复制 `ld_modbus` 和那一小段应用代码，却没有同时复制 LDC 的头文件及实现，会出现找不到 `ldc_easy.h`、`ldc_easy_pop()` 等问题。这是示例依赖没有交代清楚，不是调用者还需要自己补写这些函数。

本次整理提供两份能够独立编译的 STM32H563 工程，并把所需 `ld_modbus` 源码直接放入工程中。下载者可以选择熟悉的 CubeMX/HAL 工程，也可以查看作者自己的分层 BSP 接入方式。

## 建议了解一下 LDC

虽然新示例默认不要求使用 LDC，但仍建议关注这个库。LDC 不是 Modbus 专用协议栈，而是一层可复用的串行数据接收、缓存和分帧组件，适合把中断/DMA 收到的字节流与上层协议处理解耦。

它的价值主要在于：

- 使用调用者提供的静态内存，不依赖堆分配；
- 支持环形缓存、完整数据包队列和统一的取包接口；
- 可以按静默时间提交数据包，也可以由 UART IDLE 等外部事件主动提交；
- ISR 只负责投递数据，上层在主循环或任务中取包，职责比较清晰；
- 同一套接收模型可以服务 Modbus、AT 指令、自定义串口协议等不同场景。

原 H5 工程使用 LDC 的静默超时作为 Modbus RTU 的 T3.5 帧结束条件，这在正常连续发送的设备通信中很实用。不过，LDC 是通用分帧库，它原本没有专门的 Modbus T1.5 违规接口。若项目要求严格执行 RTU 的 T1.5/T3.5 规则，可以使用现在随 `ld_modbus` 提供的 `ld_modbus_rtu_framer`。

两种接收路径应当二选一：

```text
轻量通用路径：UART -> LDC（T3.5/外部提交） -> ld_modbus

严格 RTU 路径：UART + 字符完成时间戳
             -> ld_modbus_rtu_framer（T1.5/T3.5）
             -> ld_modbus
```

不要把同一批 UART 字节同时交给 LDC 和 RTU framer 做两次分帧。如果确实需要 LDC 的跨任务队列能力，应先由 framer 生成完整 RTU ADU，再把“完整帧”作为一个数据包交给 LDC；此时 LDC 不再负责第二次静默分帧。

## 工程一：自定义分层 BSP 最小从站

本地目录：`STM32H563_ld_modbus_app`

这个工程用于展示作者自己的 BSP 分层方式，默认是裸机、非 DMA、USART2/RS485-1 Modbus RTU 从站，程序从 `0x08000000` 启动。

建议按以下顺序阅读：

1. `README.md`：工程约束、LDC 历史关系、TIM2/DWT 选择；
2. `Core/Src/main.c`：调用 `modbus_app_init()` 和 `modbus_app_poll()`；
3. `user/app/modbus_app_config.h`：从站地址、串口、波特率、字符位数；
4. `user/app/modbus_app.c`：UART 时间戳、RTU framer 和 `ld_modbus` 的完整接入；
5. `user/bsp/bsp_microtime.c`：TIM2/DWT 微秒时间接口；
6. `user/ldc`：完整保留的 LDC 源码，可用于了解旧接收路径。

默认运行入口：

```c
modbus_app_init();

for (;;)
{
    modbus_app_poll();
}
```

默认配置 `MODBUS_APP_USE_LDC = 0`，即使用严格 `ld_modbus_rtu_framer`，不会调用 LDC。LDC 源码仍随工程提供，避免读者找不到原依赖，也方便对比两种架构。

编译：

```powershell
cd STM32H563_ld_modbus_app
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Keil 工程入口：`MDK-ARM/STM32H563_ld_modbus_app.uvprojx`。

## 工程二：CubeMX/HAL 示例与自动测试

本地目录：`STM32H563_CubeMX_Modbus_Loopback`

这是更适合普通 STM32CubeMX 使用者的工程。时钟、GPIO、USART2、UART4 和 TIM2 均由 CubeMX 生成，默认不依赖作者的 BSP，也不依赖 LDC。

建议按以下顺序阅读：

1. `README.md`：引脚、外设、测试接线和验证结果；
2. `STM32H563_CubeMX_Modbus_Loopback.ioc`：CubeMX 外设配置；
3. `Core/Src/main.c`：完成 `MX_*_Init()` 后调用统一示例入口；
4. `App/modbus_example_config.h`：选择最小从站或双串口自动回环；
5. `App/modbus_example.c`：两个示例的统一路由；
6. `App/modbus_slave_example.c`：适合移植的最小 USART2 从站；
7. `Tests/modbus_loopback.c`：USART2 主站与 UART4 从站自动测试状态机。

统一运行入口：

```c
modbus_example_init();

for (;;)
{
    modbus_example_poll();
}
```

默认编译最小从站：

```c
#define MODBUS_EXAMPLE_MODE MODBUS_EXAMPLE_MODE_SLAVE
```

如需运行 USART2 主站与 UART4 从站的自动回环测试，修改为：

```c
#define MODBUS_EXAMPLE_MODE MODBUS_EXAMPLE_MODE_LOOPBACK
```

切换位置只有 `App/modbus_example_config.h`。较大的测试状态机被单独放在 `Tests`，普通移植者不需要阅读或复制它。

编译：

```powershell
cd STM32H563_CubeMX_Modbus_Loopback
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

CubeMX 入口：`STM32H563_CubeMX_Modbus_Loopback.ioc`。

Keil 工程入口：`MDK-ARM/STM32H563_CubeMX_Modbus_Loopback.uvprojx`。

## 两个工程怎么选择

| 需求 | 建议工程 |
| --- | --- |
| 想快速看懂并移植到普通 CubeMX/HAL 工程 | CubeMX/HAL 示例 |
| 想了解作者的 BSP、UART 和微秒时间分层 | 自定义 BSP 示例 |
| 只需要最小 Modbus RTU 从站 | 任一工程的默认模式 |
| 需要验证 T1.5/T3.5 和多波特率 | CubeMX 工程的 Loopback 模式 |
| 想研究原工程为何出现 `ldc_easy_*` 调用 | 自定义 BSP 工程中的 `user/ldc` |

两份工程都包含完整的 `Middlewares/ld_modbus` 源码副本，都不使用动态内存。`ld_modbus` 协议核心与 STM32 HAL、BSP、LDC、TIM2/DWT 保持解耦。

## 已完成验证

- `ld_modbus` GCC 与 MinGW 主机测试全部通过；
- 两份 Keil ArmClang 工程均为 `0 Error(s), 0 Warning(s)`；
- USART2 与 UART4 实际 RS485 回环在 9600、19200、115200 下完成 18 项检查；
- T1.5 违规丢弃、T3.5 恢复、CRC 错误、错误从站地址和寄存器读写均已覆盖；
- 实测 UART 错误为 0，接收溢出为 0。

这份说明用于解释依赖和工程入口。协议接口、支持功能码及移植约束仍以 `ld_modbus` 根目录 README 和 `docs/architecture.md` 为准。
