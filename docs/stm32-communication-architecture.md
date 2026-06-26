# STM32 通信架构设计手册

这份文档不是接口说明，而是解释为什么这个项目要这样设计。目标是以后遇到 UART、USB、RS485、AT 模组、LDC、Message Bus、ISR、RTOS 任务延迟问题时，能先判断边界，再写代码。

## 目录

1. [工程里的真正问题](#1-工程里的真正问题)
2. [一条数据从硬件到业务的路径](#2-一条数据从硬件到业务的路径)
3. [ISR 为什么不能干太多活](#3-isr-为什么不能干太多活)
4. [为什么需要 LDC](#4-为什么需要-ldc)
5. [为什么 endpoint 要独立于 LDC](#5-为什么-endpoint-要独立于-ldc)
6. [1 ms 轮询浪费在哪里](#6-1-ms-轮询浪费在哪里)
7. [Message Bus 解决什么，不解决什么](#7-message-bus-解决什么不解决什么)
8. [AT 模组为什么要拆 core/module/app](#8-at-模组为什么要拆-coremoduleapp)
9. [BSP 和 CubeMX 的正确关系](#9-bsp-和-cubemx-的正确关系)
10. [Shell、日志和小栈线程的坑](#10-shell日志和小栈线程的坑)
11. [推荐的新增外设流程](#11-推荐的新增外设流程)
12. [检查清单](#12-检查清单)

## 1. 工程里的真正问题

STM32 项目最容易出问题的地方不是“某个 HAL 函数不会用”，而是边界不清：

- CubeMX 生成代码和手写代码混在一起。
- BSP 里开始放协议逻辑。
- ISR 或 USB 回调里做了耗时工作。
- 每加一个外设就新增一套 task、queue、parser、log。
- LDC 本来是轻量收包核心，后续被塞进业务判断。
- shell 命令在小栈线程里格式化大结构，偶发 hard fault。

这些问题短期都能跑，长期会变成三类成本：

1. 改一个外设影响一片。
2. debug 时不知道数据卡在哪一层。
3. 移植到新板子时必须整包搬过去。

所以这个项目采用的原则是：硬件、收包、协议、业务状态机、系统事件分别拥有自己的边界。

## 2. 一条数据从硬件到业务的路径

推荐路径：

```text
UART / USB / DMA / IDLE interrupt
        -> HAL callback / BSP adapter
        -> ldc_write() / ldc_putc()
        -> LDC ring + packet queue
        -> endpoint event flag
        -> service task wakeup
        -> protocol parser / AT session / Modbus
        -> device state machine
        -> optional event bridge / message bus
        -> shell/log/status
```

这条路径看起来比“回调里直接解析”更长，但它把风险拆开了：

- 硬件层只关心字节到了。
- LDC 只关心帧边界。
- endpoint 只关心唤醒任务。
- 协议层只关心语义。
- app 只关心状态变化。

当出问题时，可以按层排查：

- 没进 ISR：硬件、引脚、DMA、NVIC 问题。
- LDC overflow：消费慢或 buffer 小。
- packet ready 但 task 不醒：endpoint/RTOS 事件问题。
- task 醒了但协议失败：parser/session 问题。
- 协议成功但业务失败：状态机问题。

## 3. ISR 为什么不能干太多活

ISR 的正确工作：

- 清中断。
- 记录错误。
- 取 DMA producer 位置。
- 把新字节交给 LDC。
- 设置 event flag 或 task notification。

ISR 不应该做：

- `printf`。
- shell 命令执行。
- AT 命令等待。
- Modbus/MQTT/JSON 解析。
- flash 擦写。
- 等 mutex。
- 阻塞式 USB/UART 发送。

原因有三个。

第一，ISR 抢占正常任务。ISR 做得越久，系统可调度时间越少。

第二，ISR 栈和优先级环境受限。很多库函数在普通线程里没问题，在 ISR 里会出隐藏问题。

第三，ISR 里的 bug 难复现。它经常表现为偶发丢包、hard fault、USB 卡死、日志断尾。

正确优化方向不是“把解析搬到 ISR 更快”，而是“ISR 尽快唤醒正确任务”。

## 4. 为什么需要 LDC

LDC 的价值是统一“流式输入到包”的过程。

UART、USB、RS485、AT 模组都有一个共同问题：硬件给你的不是完整业务包，而是一段段字节流。

没有 LDC 时，每个外设都会重复写：

- ring buffer。
- 长度判断。
- 分隔符查找。
- 超时提交。
- overflow 处理。
- packet 队列。
- 统计和 debug。

这会导致每个设备都有一套略微不同、略微有 bug 的收包逻辑。

LDC 统一负责：

- 字节输入。
- 块输入。
- ring。
- packet descriptor。
- delimiter / fixed length / timeout / flush。
- overflow/drop/peak 统计。

LDC 不负责：

- AT 的 `OK/ERROR` 判断。
- Modbus CRC 语义。
- MQTT 包解析。
- shell 命令。
- 业务状态机。

保持这个边界，LDC 才能移植到别的 STM32 工程。

## 5. 为什么 endpoint 要独立于 LDC

LDC core 不应该包含 ThreadX，因为这样会让它失去可移植性。

所以拆出 endpoint：

```text
LDC core: 纯 C，负责收包
endpoint: 适配 ThreadX event flag / semaphore / task wakeup
service: 消费 packet 并解析协议
```

这带来三个好处：

1. 裸机工程可以只用 LDC core。
2. ThreadX 工程可以用 event flag 避免轮询。
3. 以后换 FreeRTOS，只换 endpoint，不动 LDC。

这就是“核心库不插 RTOS 内容，但系统仍然事件驱动”的折中。

## 6. 1 ms 轮询浪费在哪里

以前常见写法：

```text
while (1) {
    tx_thread_sleep(1);
    ldc_tick();
    if (packet_ready) {
        parse();
    }
}
```

问题是：包已经到了，但任务最多要等 1 个 tick 才发现。

如果 ThreadX tick 是 1 kHz，那么：

- 平均检测延迟约 0.5 ms。
- 最坏检测延迟接近 1 ms。
- 多个服务各自 1 ms wakeup，会产生无意义调度。

事件驱动写法：

```text
RX callback -> LDC packet ready -> set event flag -> task wakes immediately
```

延迟变为：

- ISR/callback 执行时间。
- RTOS event flag 唤醒开销。
- 当前高优先级任务让出 CPU 的时间。

这通常明显低于固定 1 ms 轮询，尤其是 AT 响应、shell 输入、短 Modbus 帧这类小包。

但要注意：事件驱动不是零延迟。它只是去掉“人为等下一个 tick 才检查”的延迟。

经验估算：

| 模式 | 典型额外检测延迟 | 代价 |
| --- | ---: | --- |
| 1 ms 轮询 | 平均约 0.5 ms，最坏约 1 ms | 简单，但空转多 |
| event flag 唤醒 | 通常为调度级延迟 | 需要 endpoint 适配 |
| ISR 内直接解析 | 看似最低 | 风险最高，不推荐 |

最终选择：

- 小工程、低速、逻辑少：轮询可以接受。
- 多 UART/USB/AT/RS485：endpoint event 更稳。
- 硬实时采样：另做 DMA/硬件专用路径，不要塞进通用 LDC。

## 7. Message Bus 解决什么，不解决什么

Message Bus 解决的是模块之间的事件依赖，不是原始通信收包。

它适合传：

- W800 状态变化。
- NearLink 角色切换结果。
- RS485 overflow。
- USB vendor 收到控制命令。
- log/status 小消息。

它不适合传：

- 大块 UART payload。
- 大 OTA 数据。
- 指向栈内存的 packet。
- 需要长时间处理的任务。

正确关系：

```text
LDC: 收包、分帧、排队
Message Bus: 轻量事件、状态、控制通知
```

为什么默认关闭？

因为设备少时，直连更简单、更快、更容易看懂：

```c
#define APP_ENABLE_MSG_BUS 0U
```

当设备数量增加，出现这些迹象再打开：

- 多个模块互相直接调用。
- 日志出口散落。
- shell 状态查询要碰很多服务。
- 一个状态变化要通知多个消费者。

## 8. AT 模组为什么要拆 core/module/app

AT 模组有共性，也有强差异。

共性：

- 发送命令。
- 等待响应。
- 超时。
- 匹配 `OK/ERROR`。
- 处理 URC。
- 按步骤配置。

差异：

- W800 的 Wi-Fi 命令。
- NearLink 的主从角色命令。
- EC20 的蜂窝网络命令。
- 不同模组 reset 后 ready 时间不同。
- 有些配置会保存，有些不会保存。

所以拆成：

```text
at_core: session / timeout / response / URC
at_command_plan: 必选/可选配置步骤表
at_module_xxx: 具体命令和响应解析
app_xxx: reset、状态机、shell 配置、重试策略
```

这能避免两个极端：

- 每个模组复制一套 AT 框架。
- 过早做一个假的“万能网络接口”导致抽象不准。

## 9. BSP 和 CubeMX 的正确关系

CubeMX 用来保存硬件配置，不应该成为业务架构。

正确做法：

1. `.ioc` 记录 pin/peripheral。
2. `Core/Src/*.c` 初始化 HAL 外设。
3. BSP 绑定 HAL handle 和板级引脚。
4. app 只调用 BSP/service 接口。

例如 LCD：

```text
CubeMX: SPI2 + GPIO pin 配置
BSP: bsp_lcd_init(), bsp_lcd_fill_color()
App: 不关心 PB10/PC1/PD11 是什么
```

这样 CubeMX 重新生成后，只需要检查生成层和 BSP 层，不会让业务逻辑散落到 `main.c` 或 `gpio.c`。

## 10. Shell、日志和小栈线程的坑

Shell 很容易被低估。

一个看似简单的命令：

```text
modbus status
```

可能触发：

- 读取大状态结构。
- 格式化多个 64 位计数。
- `vsnprintf` 使用较多栈。
- USB CDC 发送。

如果这个命令在 USBX 小栈线程里直接执行，就可能 hard fault。

更好的做法：

- USB RX 只收一行命令。
- 命令投递到 shell task。
- shell task 执行命令。
- 大状态分段打印。
- 日志通过低优先级输出通道发送。

原则：通信接收线程不应该顺手变成业务执行线程。

## 11. 推荐的新增外设流程

以后加一个新外设，按这个顺序：

1. 写清楚硬件连接和外设所有权。
2. 在 `.ioc` 增加或确认 pin/peripheral。
3. 在 BSP 增加 reset/CS/backlight/DE 等板级控制。
4. 如果是字节流设备，接入 LDC endpoint。
5. 如果是 AT 设备，新增 `at_module_xxx` 和 `app_xxx`。
6. 如果需要跨模块通知，再加 Message Bus 事件。
7. 增加 shell 命令只作为控制/查询入口，不直接写复杂业务。
8. build。
9. 必要时 flash。
10. git 只提交本次相关文件。

## 12. 检查清单

写代码前：

- 当前 git 是否干净？如果不干净，哪些是用户改动？
- 这个外设用哪个 UART/SPI/I2C？有没有冲突？
- CubeMX `.ioc` 是否同步？
- BSP 是否足够表达板级硬件？
- 业务逻辑有没有误塞到 BSP？
- LDC core 有没有被 RTOS/HAL 污染？

写代码后：

- 是否有大块 payload 复制进 Message Bus？
- ISR/callback 是否只做短动作？
- shell 命令是否可能跑在小栈线程？
- Keil 是否 `0 Error(s), 0 Warning(s)`？
- `.uvprojx` 是否只加入必要文件？
- staged diff 是否只包含本次改动？

如果这些问题都能回答清楚，后续项目会少走很多弯路。
