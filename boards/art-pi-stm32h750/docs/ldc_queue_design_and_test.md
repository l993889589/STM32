---
title: ART-Pi LDC Queue 事件驱动串口接入与方案对比
category: STM32
tags:
  - STM32H750
  - ART-Pi
  - UART4
  - DMA
  - ThreadX
  - LDC
  - Liqueue
summary: 将 UART4 LDC 测试从轮询 pop 改成 LDC 成包事件、ThreadX semaphore 和软件 timer，并从效率、安全、数据流向、调试边界等角度比较 ldc_queue、旧 endpoint/channel、Liqueue 三种方式。
project: D:\Embedded\artpi
created: 2026-06-30
build:
  cwd: D:\Embedded\artpi\MDK-ARM
  command: 'C:\Keil_v5\UV4\UV4.exe -r ARTPI.uvprojx -o build_ldc_queue_final_dma.log'
---

# ART-Pi LDC Queue 事件驱动串口接入与方案对比

## 本次完成的 6 项任务

1. `ldc_queue` 从任务轮询改为事件驱动：LDC core 成包后触发 `LDC_QUEUE_EVT_PACKET`。
2. `ldc_queue` 增加事件回调：支持 packet、overflow、drop 三类事件。
3. UART4 增加 ThreadX timer 管理 LDC silence timeout：timer 只在存在半包数据时调用 `ldc_queue_tick()`。
4. UART4 测试任务改为 semaphore 驱动：任务阻塞在 `tx_semaphore_get()`，成包事件唤醒后再 pop。
5. 已编译、烧录并用 ST-LINK VCP 串口验证 DMA 块接收和非 DMA 字节接收。
6. 生成本说明文档，用于后续把其他串口或外设迁移到 `ldc_queue`。

## 当前结论

新串口或新外设建议优先使用 `bsp_uart + ldc_queue + RTOS 通知对象` 这一套。

原因很直接：

- `ldc_queue` 的使用方式接近 Liqueue，入口清楚，初始化、装填、tick、pop 都在一个 handle 上完成。
- 底层仍然使用现有 LDC core，不直接搬 Liqueue 的不安全实现。
- UART4 接收任务不再 1 tick 轮询 pop，空闲时真正阻塞。
- DMA 和非 DMA 都走同一个上层队列，区别只在 `ldc_queue_bind_uart()` 的 `rx_mode`。
- CubeMX 继续只负责生成 `huart4`、GPIO、DMA、IRQ；用户代码主要在 `bsp_uart`、`ldc_queue`、app service，不会把协议逻辑散进 CubeMX 文件。

旧 `ldc_endpoint_threadx` 和 `app_serial_ldc` 现在仍保留，因为 USB、RS485 等模块还在引用它们。本次 UART4 测试路径已经不再依赖 endpoint/channel。

## 当前 UART4 测试路径

当前工程开关：

```c
#define APP_ENABLE_UART4_LDC_TEST          1U
#define APP_UART4_ECHO_RX_DMA              1U
```

启动路径：

```text
Core/Src/app_threadx.c
  -> bsp_init()
  -> app_uart4_echo_init()
  -> tx_semaphore_create()
  -> ldc_queue_init()
  -> ldc_queue_bind_uart()
  -> tx_timer_create()
  -> tx_thread_create()
  -> app_uart4_echo_task_entry()
  -> ldc_queue_uart_start()
```

UART4 默认参数：

```text
baudrate      : 115200
rx mode       : DMA ReceiveToIdle
ring size     : 512 + 1 bytes
packet count  : 8
max frame     : 256 bytes
timeout       : 20 ms
thread prio   : 14
timer prio    : ThreadX port default 0, 当前 tx_user.h 未覆盖
test VCP      : COM19, STMicroelectronics STLink Virtual COM Port
```

## 数据流向

### DMA 块接收

```text
UART4 RX DMA + IDLE
  -> HAL_UARTEx_RxEventCallback()
  -> bsp_uart_rx_cb_t
  -> ldc_queue_add()
  -> ldc_write()
  -> LDC 成包或缓存半包
  -> LDC_QUEUE_EVT_PACKET
  -> tx_semaphore_put()
  -> UART4 LDC Queue task 被唤醒
  -> ldc_queue_pop()
  -> app_uart4_write()
```

### 非 DMA 字节接收

```text
UART4 RXNE interrupt
  -> HAL_UART_RxCpltCallback()
  -> bsp_uart_rx_cb_t
  -> ldc_queue_putc()
  -> ldc_putc()
  -> LDC 成包或缓存半包
  -> LDC_QUEUE_EVT_PACKET
  -> tx_semaphore_put()
  -> UART4 LDC Queue task 被唤醒
  -> ldc_queue_pop()
```

### silence timeout 成包

```text
TX_TIMER: UART4 LDC Tick
  -> ldc_queue_need_tick()
  -> ldc_queue_tick(elapsed_ms)
  -> ldc_tick()
  -> timeout 到达后提交半包
  -> LDC_QUEUE_EVT_PACKET
  -> tx_semaphore_put()
```

注意：LDC core 的事件回调是在退出 LDC 临界区之后调用的。因此当前回调里做 `tx_semaphore_put()` 不会发生在关中断临界区内。回调里仍然必须保持短小，不做 printf、不 pop、不解析协议。

## 为什么还需要 tick

LDC 支持三种成包方式：

1. 遇到 delimiter 成包，例如 `'\n'`。
2. 达到 `max_frame` 成包。
3. 接收暂停超过 `timeout_ms` 成包。

UART4 当前 `delimiter = -1`，主要依靠 20 ms 空闲超时成包。所以没有 tick 就无法判断“这一串字节结束了”。本次改动不是去掉 tick，而是把 tick 从消费任务里移到 ThreadX timer，并且通过 `ldc_queue_need_tick()` 避免无半包时空跑。

## 为什么 init 后还要 start

保留 `init/bind/start` 三段是刻意的：

- `ldc_queue_init()` 只初始化 LDC 内存、成包策略、事件回调。
- `ldc_queue_bind_uart()` 只把队列和 `bsp_uart` 端口绑定，注册 RX 回调。
- `ldc_queue_uart_start()` 才真正启动硬件接收。

这样做的好处是 RTOS 对象可以先创建，回调路径也先准备好，再打开 RX 中断/DMA。否则如果 `init()` 里直接启动接收，硬件可能在 semaphore 或线程还没准备好时就进数据，调试会更困难。

## 接一个新串口的推荐模板

### 1. CubeMX/HAL 层

CubeMX 只负责外设基础配置：

- 生成 `UART_HandleTypeDef huartx`。
- 如果使用 DMA，生成 RX DMA 和 IRQ。
- 打开 UART IRQ。
- 不在 CubeMX 文件里写协议解析。

### 2. BSP 绑定 HAL handle

在 `bsp_init()` 或板级初始化里绑定一次：

```c
bsp_uart_bind(BSP_UART4, &huart4, 1U, 0U);
```

`use_dma = 1U` 表示 `bsp_uart_start_rx()` 使用 `HAL_UARTEx_ReceiveToIdle_DMA()`。字节中断模式仍然可以通过 `ldc_queue` 选择 `LDC_QUEUE_UART_RX_BYTE_IT`。

### 3. app 层提供静态内存

```c
static ldc_queue_t g_uart_queue;
static TX_SEMAPHORE g_uart_packet_sem;
static TX_TIMER g_uart_tick_timer;

static uint8_t g_uart_rx_dma[128] __ALIGNED(32);
static uint8_t g_uart_ring[513];
static ldc_packet_t g_uart_packets[8];
static uint8_t g_uart_frame[256];
```

### 4. 事件回调只负责通知任务

```c
static void uart_queue_event(ldc_queue_t *queue,
                             ldc_queue_event_t event,
                             void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_QUEUE_EVT_PACKET)
        (void)tx_semaphore_put(&g_uart_packet_sem);
}
```

如果换成 FreeRTOS，这里对应 `xSemaphoreGiveFromISR()` 或 task notification。如果换成裸机，这里对应置位一个 volatile flag。

### 5. timer 只做超时结算

```c
static void uart_tick_timer_entry(ULONG timer_input)
{
    static uint32_t last_ms;
    uint32_t now_ms = HAL_GetTick();
    uint32_t elapsed_ms = now_ms - last_ms;
    last_ms = now_ms;

    (void)timer_input;

    if(elapsed_ms != 0U && ldc_queue_need_tick(&g_uart_queue))
        ldc_queue_tick(&g_uart_queue, elapsed_ms);
}
```

timer 回调里不要 pop、不要写串口、不要调用可能阻塞的业务函数。

### 6. 任务里等待完整 frame

```c
for(;;)
{
    (void)tx_semaphore_get(&g_uart_packet_sem, TX_WAIT_FOREVER);

    for(;;)
    {
        int length = ldc_queue_pop(&g_uart_queue,
                                   g_uart_frame,
                                   sizeof(g_uart_frame));
        if(length <= 0)
            break;

        /* process g_uart_frame[0..length-1] */
    }
}
```

这里使用内部 `for(;;) pop until empty` 是为了合并信号量计数和已排队 frame。即使一次唤醒时已经积累多个包，也可以一次处理完。

## DMA 和非 DMA 的切换

DMA 块装填：

```c
uart_config.rx_mode = LDC_QUEUE_UART_RX_DMA_BLOCK;
uart_config.rx_buffer = g_uart_rx_dma;
uart_config.rx_buffer_size = sizeof(g_uart_rx_dma);
```

非 DMA 字节装填：

```c
uart_config.rx_mode = LDC_QUEUE_UART_RX_BYTE_IT;
uart_config.rx_buffer = NULL;
uart_config.rx_buffer_size = 0U;
```

当前 UART4 示例用一个开关切换：

```c
#define APP_UART4_ECHO_RX_DMA              1U  /* DMA */
#define APP_UART4_ECHO_RX_DMA              0U  /* byte interrupt */
```

## 其他外设怎么使用

如果外设不是 UART，不需要调用 `ldc_queue_bind_uart()`：

```c
ldc_queue_init(&queue, &queue_config);

/* 块数据，例如 USB、SPI DMA、网络流 */
ldc_queue_add(&queue, data, length);

/* 字节数据，例如 GPIO bitbang 或单字节中断 */
ldc_queue_putc(&queue, byte);

/* 周期结算 timeout */
ldc_queue_tick(&queue, elapsed_ms);

/* 任务里读取完整 frame */
length = ldc_queue_pop(&queue, buffer, sizeof(buffer));
```

也就是说 `ldc_queue` 分两层：

- 通用 LDC queue：`init/add/putc/tick/pop/event`。
- UART 便捷绑定：`bind_uart/start/write`。

这样不会把 LDC core 绑死在 UART 上。

## 三套方案对比

| 维度 | 新 `ldc_queue` | 旧 endpoint/channel | Liqueue |
| --- | --- | --- | --- |
| 使用入口 | 一个 `ldc_queue_t`，函数式 API | endpoint、serial service、channel wrapper 分层较多 | 一个 `lq_handle`，入口清楚 |
| RTOS 唤醒 | LDC packet 事件释放 semaphore | endpoint event flags，部分上层仍 1 tick wait | 有 `genover` 回调，但常见用法仍依赖外部 tick 和 pop |
| 空闲开销 | 任务阻塞；timer 只在半包时 tick | endpoint 层可等待事件，但 app_serial 仍周期 wait 1 tick | 常见写法需要周期 tick 和 pop 轮询 |
| DMA 块装填 | `ldc_queue_add()` | `ldc_endpoint_write()` | `lq_add()` |
| 字节装填 | `ldc_queue_putc()` | `ldc_endpoint_putc()` 或 write 1 byte | `lq_add(..., 1)` |
| 小 buffer pop | 返回 `-1`，包保留 | LDC core 行为，包保留 | 会先弹节点再拷贝部分数据，存在丢包风险 |
| 并发保护 | LDC lock，当前用 PRIMASK | ThreadX interrupt control | 无内置临界区约束 |
| 内存 | 全静态，由调用者提供 | 全静态，由调用者提供 | 支持静态，也有 malloc 版本 |
| overwrite | LDC core 支持并统计 drop | LDC core 支持 | `CAN_OVERWRITE` 分支未完整实现 |
| 调试定位 | `bsp_uart -> ldc_queue -> app` | 层级更深，路径更分散 | 路径短，但安全边界弱 |
| CubeMX 覆盖风险 | HAL 在 CubeMX，业务在 user | 同样可隔离，但封装层多 | 与 CubeMX 无直接关系 |

## 效率分析

### CPU 空闲时

旧 UART4 测试逻辑类似：

```c
for(;;)
{
    ldc_queue_tick(...);
    while(ldc_queue_pop(...) > 0) { ... }
    tx_thread_sleep(1);
}
```

这会导致即使没有数据，任务也每个 tick 醒来一次。

现在 UART4 任务是：

```c
tx_semaphore_get(&g_uart4_packet_sem, TX_WAIT_FOREVER);
```

没有完整 frame 时任务不运行。唯一周期动作是 ThreadX timer；timer 里先检查 `ldc_queue_need_tick()`，没有半包就直接返回。

### RX 中断/DMA 回调时

DMA 模式下，一次 IDLE 事件提交一块数据，通常比单字节中断省 CPU。

字节模式下，每个字节都会进入 `HAL_UART_RxCpltCallback()`，效率低于 DMA，但代码路径相同，适合验证、低速串口或不方便开 DMA 的外设。

### 成包后

成包事件只做两件事：

1. `g_uart4_packet_signals++`
2. `tx_semaphore_put(&g_uart4_packet_sem)`

真正的 `pop`、协议解析、串口打印都在任务上下文执行。

## 安全分析

### LDC core 的安全边界

- 所有内存由调用者静态提供，不使用 malloc/free。
- `ldc_read_packet()` 在用户 buffer 太小时返回 `-1`，不弹出 packet，调用方可以换更大 buffer 重试。
- LDC core 有 `rx_bytes`、`packets`、`overflow`、`drop`、`packet_peak` 等统计。
- 事件通知在退出 LDC 临界区后发出。
- 当前 `ldc_queue` lock 使用 PRIMASK，适合 ISR 和任务共享一个 queue 的场景。

### 当前还需要注意的边界

- 事件回调可能来自 UART IRQ，也可能来自 ThreadX timer thread。回调里只能做 ISR-safe 或非阻塞动作。
- `tx_semaphore_put()` 当前用于唤醒任务；如果移植到其他 RTOS，要确认对应 release API 是否支持 ISR 上下文。
- Timer thread 优先级当前为 ThreadX 默认 0。由于回调非常短，这样可以接受；不要在 timer 回调里加入耗时逻辑。
- UART4 DMA buffer 当前在 `D1/DTCM/cache` 环境下需要继续关注 cache 属性。本工程 `cache_invalidate` hook 目前为空，UART4 当前测试可用，但如果后续把 DMA buffer 放入 cacheable 区，要补 DCache invalidate。

## Liqueue 为什么不直接使用

`D:\Embedded\EC20\mid\liqueue.*` 的优点是“绑定方式清楚”：

```c
lq_mem_array_init(&queue, pool, pool_size, node_pool, node_count);
lq_timer_init(&queue, timeout);
lq_init(&queue, packet_size, NO_OVERWRITE);
lq_add(&queue, data, len);
lq_tick(&queue, 1);
len = lq_pop(&queue, buf, sizeof(buf));
```

但当前实现有几个不适合作为新基础库的问题：

- `lq_mem_array_init()` 成功路径返回 `0`，失败也返回 `0`，调用方无法可靠判断初始化结果。
- `lq_pop()` 先弹出 node，再按用户 buffer 大小拷贝；buffer 太小时会返回截断数据，并丢掉剩余数据。
- `lq_read()` 是忙等；`lq_read_ex()` 也是循环等待，只是每轮调用 `waittask()`。
- 没有内置临界区，ISR 和任务同时访问时需要调用方自己保证。
- `CAN_OVERWRITE` 分支未完整实现。
- malloc 版本存在堆碎片和失败处理问题，不适合当前这种板级驱动基础层。

所以本次不是替换成 Liqueue，而是复刻它“一个 handle、一组函数”的清晰调用风格，同时保留 LDC core 的安全策略。

## endpoint/channel 为什么不作为新串口默认方式

旧 endpoint/channel 不是错误方案，它的问题主要是接入复杂度。

典型路径：

```text
bsp_uart
  -> app_serial_ldc
  -> ldc_endpoint_threadx
  -> ldc_core
  -> app frame callback
```

或者早期 channel 宏路径：

```text
DEFINE_LDC_CHANNEL(...)
  -> prefix_init()
  -> prefix_tick()
  -> prefix_task()
  -> prefix_message_in()
```

它的优势：

- endpoint 已经有 ThreadX event flags。
- 基于 LDC core，安全边界比 Liqueue 好。
- 已经被 USB、RS485 等模块使用，短期不适合强行删除。

它的不足：

- 初始化参数分散，串口、TX queue、RX DMA、endpoint、frame callback、thread stack 都在一个更大的 app wrapper 里。
- app_serial 的发送队列按字节入队，适合通用异步串口服务，但对简单测试串口显得重。
- 部分任务仍用 `ldc_endpoint_wait_for(..., 1U)` 这种周期等待写法，理解成本比一个 semaphore 示例更高。
- 早期 `DEFINE_LDC_CHANNEL` 宏展开不直观，不利于你后续排查某个串口到底绑定了哪些文件。

因此建议：

- 已经稳定使用 endpoint/channel 的模块先不动。
- 新增串口、新增简单外设、临时测试，优先用 `ldc_queue`。
- 后续如果要统一架构，可以逐个把 endpoint/channel 服务迁移到 `ldc_queue`，不要一次性重构全部模块。

## RTOS tick 和 timer 优先级建议

如果把多个 queue 的 `tick` 放在一个软件 timer 或 SysTick 后处理里，优先级原则如下：

1. tick 处理必须短，只做 `need_tick -> tick`，不要 pop、printf、协议解析。
2. timer 优先级可以高于普通业务任务，因为它只是结算时间边界。
3. 如果 timer 回调里 queue 数量很多，应该拆分或降低频率，避免影响系统调度。
4. 对串口 timeout 成包来说，1 ms tick 足够；协议允许的话可以放宽到 2 到 5 ms 减少调度次数。
5. 真正的包处理任务优先级按业务实时性配置。UART4 测试当前是 14，LED 是 20，shell 是 15。

当前 ThreadX 配置没有覆盖 `TX_TIMER_THREAD_PRIORITY`，所以使用 port 默认 priority 0。因为本工程 timer callback 很短，目前可接受。后续如果在 timer callback 里加入大量 queue，建议先把 callback 改成只发一个全局事件，由专门的低优先级 tick task 处理。

## CubeMX 覆盖风险

CubeMX 可以覆盖的主要是：

```text
Core/Src/usart.c
Core/Src/dma.c
Core/Src/gpio.c
Core/Src/stm32h7xx_it.c
Core/Inc/*.h
```

本次 `ldc_queue` 方案把业务代码放在：

```text
user/bsp/bsp_uart.c
user/bsp/bsp_uart.h
user/comm/ldc_queue/ldc_queue.c
user/comm/ldc_queue/ldc_queue.h
user/app/app_uart4_echo.c
user/app/app_config.h
user/app/app_ldc_config.c
```

后续 CubeMX 重新生成后重点检查：

- `MX_UART4_Init()` 里的波特率是否仍是 `APP_UART4_ECHO_BAUDRATE`。
- UART4 RX DMA 是否仍是 `DMA1_Stream0`、`DMA_REQUEST_UART4_RX`。
- `UART4_IRQn` 和 DMA IRQ 是否仍启用。
- `bsp_init()` 是否仍绑定 `BSP_UART4` 到 `&huart4`。
- Keil 工程里是否仍包含 `ldc_queue.c`，include path 是否仍有 `..\user\comm\ldc_queue`。

## 本次代码变化

核心文件：

```text
D:\Embedded\artpi\user\comm\ldc_queue\ldc_queue.h
D:\Embedded\artpi\user\comm\ldc_queue\ldc_queue.c
D:\Embedded\artpi\user\app\app_uart4_echo.c
D:\Embedded\artpi\user\app\app_config.h
```

关键新增接口：

```c
typedef enum
{
    LDC_QUEUE_EVT_PACKET = 0,
    LDC_QUEUE_EVT_OVERFLOW,
    LDC_QUEUE_EVT_DROP
} ldc_queue_event_t;

typedef void (*ldc_queue_event_cb_t)(ldc_queue_t *queue,
                                     ldc_queue_event_t event,
                                     void *arg);

void ldc_queue_set_event_callback(ldc_queue_t *queue,
                                  ldc_queue_event_cb_t callback,
                                  void *arg);

bool ldc_queue_need_tick(ldc_queue_t *queue);
```

UART4 app 关键变化：

```text
g_uart4_packet_sem      : packet 事件唤醒任务
g_uart4_tick_timer      : silence timeout 结算
app_uart4_queue_event() : packet/drop/overflow 事件入口
app_uart4_tick_timer_entry()
app_uart4_echo_task_entry()
```

## 验证记录

### 1. DMA event-driven 构建

```powershell
C:\Keil_v5\UV4\UV4.exe -r ARTPI.uvprojx -o build_ldc_queue_event.log
```

结果：

```text
Program Size: Code=52630 RO-data=1090 RW-data=68 ZI-data=17812
"ARTPI\ARTPI.axf" - 0 Error(s), 0 Warning(s).
```

烧录：

```powershell
C:\Keil_v5\UV4\UV4.exe -f ARTPI.uvprojx -o flash_ldc_queue_dma.log
```

结果：

```text
Erase Done.Programming Done.Verify OK.Application running ...
Flash Load finished at 08:11:07
```

串口测试：

```text
port    : COM19
baud    : 115200 8N1
payload : ldc_dma_test_002
output  : [uart4-ldc-queue] frame: ldc_dma_test_002
```

### 2. 非 DMA 字节接收构建

临时切换：

```c
#define APP_UART4_ECHO_RX_DMA              0U
```

构建：

```powershell
C:\Keil_v5\UV4\UV4.exe -r ARTPI.uvprojx -o build_ldc_queue_byte.log
```

结果：

```text
Program Size: Code=52630 RO-data=1090 RW-data=68 ZI-data=17636
"ARTPI\ARTPI.axf" - 0 Error(s), 0 Warning(s).
```

烧录：

```powershell
C:\Keil_v5\UV4\UV4.exe -f ARTPI.uvprojx -o flash_ldc_queue_byte.log
```

结果：

```text
Erase Done.Programming Done.Verify OK.Application running ...
Flash Load finished at 08:12:38
```

串口测试：

```text
payload : ldc_byte_test_001
output  : [uart4-ldc-queue] frame: ldc_byte_test_001
```

### 3. 恢复最终 DMA 默认配置

最终配置：

```c
#define APP_UART4_ECHO_RX_DMA              1U
```

构建：

```powershell
C:\Keil_v5\UV4\UV4.exe -r ARTPI.uvprojx -o build_ldc_queue_final_dma.log
```

结果：

```text
Program Size: Code=52630 RO-data=1090 RW-data=68 ZI-data=17812
"ARTPI\ARTPI.axf" - 0 Error(s), 0 Warning(s).
```

烧录：

```powershell
C:\Keil_v5\UV4\UV4.exe -f ARTPI.uvprojx -o flash_ldc_queue_final_dma.log
```

结果：

```text
Erase Done.Programming Done.Verify OK.Application running ...
Flash Load finished at 08:13:31
```

最终串口测试：

```text
payload : ldc_dma_final_001
output  : [uart4-ldc-queue] frame: ldc_dma_final_001
```

当前板子上烧录的是最终 DMA 默认版本。

## 后续建议

1. 保留 endpoint/channel 一段时间，先不要为了统一而删除正在被 USB/RS485 使用的代码。
2. 新增串口或简单外设时用 `ldc_queue`，跑通后再决定是否迁移旧模块。
3. 如果多个 app 都重复 semaphore/timer/thread 样板，可以再加一个很薄的 `ldc_queue_threadx_service`，但不要把 HAL handle、CubeMX init 和协议解析塞进 LDC core。
4. AP6212 后续联网验证建议独立成 Wi-Fi 固件加载和 NetX Duo 任务，不和 UART4 LDC 测试混在一起。
