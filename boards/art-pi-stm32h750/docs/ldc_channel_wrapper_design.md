---
title: LDC UART 接入收敛方案：CubeMX + BSP UART + app_serial_ldc
category: STM32
tags:
  - STM32
  - LDC
  - UART
  - ThreadX
  - BSP
summary: 清理实验性的 endpoint/channel 示例后，固定推荐路径为 CubeMX 初始化 huart，bsp_uart 绑定串口，app_serial_ldc 绑定 BSP 端口并向业务层输出完整 frame。
project: D:\Embedded\artpi
build:
  cwd: D:\Embedded\artpi\MDK-ARM
  command: 'C:\Keil_v5\UV4\UV4.exe -r ARTPI.uvprojx'
---

# LDC UART 接入收敛方案

## 结论

当前工程不再推荐额外的 `ldc_uart_channel_threadx` 封装，也不再保留独立的 UART4 endpoint/channel 示例。

正式推荐路径固定为：

```text
CubeMX
  生成 huart4 / huart3、DMA、IRQ、GPIO、时钟

bsp_uart
  绑定 HAL UART handle，统一处理 ReceiveToIdle 和 HAL 回调分发

app_serial_ldc
  绑定 BSP_UART4 / BSP_USART3，内部使用 LDC endpoint，输出完整 frame

业务 app
  只实现 frame_cb，处理完整数据帧
```

换成调用关系就是：

```text
huart4
  -> bsp_uart_bind(BSP_UART4, &huart4, 1U, 0U)
  -> app_serial_ldc_init(... .uart_port = BSP_UART4 ...)
  -> frame_cb(serial, frame, length, arg)
```

## 为什么不让 bsp_uart 直接绑定 LDC

`bsp_uart` 应该保持纯串口驱动层，只做：

```text
UART handle 绑定
DMA ReceiveToIdle 启动
HAL_UARTEx_RxEventCallback 分发
TX/RX callback 注册
阻塞/中断发送
```

如果让 `bsp_uart` 直接知道 LDC，它就会同时承担 UART 驱动、LDC 分包、ThreadX endpoint、业务 buffer 管理。这样 BSP 层会变重，后续某个串口只想做 shell 或原始收发时反而难拆。

所以 LDC 绑定点放在 `app_serial_ldc`，不是 `bsp_uart`。

## 当前保留的层次

```text
Core/Src/usart.c, Core/Src/dma.c
  CubeMX 生成，负责硬件初始化

user/bsp/bsp_uart.c
  统一 UART RX/TX 分发，不知道 LDC

user/ldc/core/*
  LDC core，只负责字节流成帧

user/comm/ldc_endpoint/threadx/*
  LDC + ThreadX event flags

user/app/app_serial_ldc.c
  唯一推荐的 UART + LDC + ThreadX 封装

user/app/app_ap6212_bridge.c
  业务层：初始化 UART4 debug 和 USART3 AP6212 BT 两个串口通道
```

## 一个串口应该怎么看

调试一个串口时，不需要从所有层开始看。建议按这个顺序：

```text
1. CubeMX/HAL 层
   huart 是否初始化，DMA/IRQ 是否打开

2. bsp_uart 层
   bsp_uart_bind() 是否绑定正确
   bsp_uart_start_rx() 是否成功
   bsp_uart_rx_events() 是否增加

3. app_serial_ldc 层
   app_serial_ldc_diag() 看 rx_bytes、ldc_bytes、frames、drops

4. LDC endpoint 层
   app_serial_ldc_get_ldc_stats() 看 packets、overflow、drop

5. 业务层
   frame_cb 是否被调用，frame 内容是否符合预期
```

## UART4/AP6212 当前用法

`app_ap6212_bridge.c` 是当前工程里最完整的正式用法。

它创建两个 `app_serial_ldc_t`：

```text
g_debug_serial       -> UART4 调试串口
g_ap6212_bt_serial   -> USART3 / AP6212 BT HCI 串口
```

UART4 初始化入口：

```c
static UINT init_debug_serial(void)
{
    const app_ldc_port_config_t *ldc_config;
    app_serial_ldc_config_t config;

    ldc_config = app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);
    if(ldc_config == NULL)
        return TX_PTR_ERROR;

    config.name = "uart4-debug";
    config.uart_port = APP_UART4_ECHO_PORT;
    config.ldc_config = ldc_config;
    config.rx_dma = g_debug_rx_dma;
    config.rx_dma_size = sizeof(g_debug_rx_dma);
    config.tx_queue_storage = g_debug_tx_queue_storage;
    config.tx_queue_depth = APP_UART4_ECHO_TX_QUEUE_DEPTH;
    config.tx_chunk = g_debug_tx_chunk;
    config.tx_chunk_size = sizeof(g_debug_tx_chunk);
    config.thread_stack = g_debug_thread_stack;
    config.thread_stack_size = sizeof(g_debug_thread_stack);
    config.thread_priority = APP_UART4_ECHO_THREAD_PRIO;
    config.ldc_ring = g_debug_ldc_ring;
    config.ldc_ring_size = sizeof(g_debug_ldc_ring);
    config.ldc_packets = g_debug_ldc_packets;
    config.ldc_packet_count = APP_UART4_ECHO_LDC_PACKET_COUNT;
    config.frame_buffer = g_debug_frame;
    config.frame_buffer_size = sizeof(g_debug_frame);
    config.flags = 0U;
    config.frame_cb = bridge_frame_to_target;
    config.frame_arg = &g_ap6212_bt_serial;

    return app_serial_ldc_init(&g_debug_serial, &config);
}
```

这段看起来字段不少，但它有一个好处：所有内存资源都在当前业务文件里显式可见，没有宏展开，没有隐式 malloc。

## UART4 LDC 测试模式

为了避免每次验证 LDC 都依赖 AP6212，当前工程增加了一个独立测试开关：

```c
#define APP_ENABLE_UART4_LDC_TEST          1U
```

开关位置：

```text
D:\Embedded\artpi\user\app\app_config.h
```

当该宏为 `1U` 时，`Core/Src/app_threadx.c` 只启动：

```c
app_service_init_status = app_uart4_echo_init();
```

不会启动：

```c
app_ap6212_bridge_init();
app_ap6212_sdio_probe_init();
```

也就是说，当前 UART4 是一个纯 LDC 测试串口，不会转发到 AP6212，也不会打印 QSPI/SDIO probe 日志。

启动后 UART4 会输出：

```text
[uart4-ldc-test] ready
```

串口工具发送任意 ASCII 或 HEX 数据后，LDC 以 `APP_UART4_ECHO_LDC_TIMEOUT_MS` 配置的 20ms 空闲超时成帧，然后 UART4 打印：

```text
[uart4-ldc-test] frame: <收到的一帧数据>
```

测试完成后，如果要恢复 AP6212 bridge，把宏改回：

```c
#define APP_ENABLE_UART4_LDC_TEST          0U
```

## 新串口接入模板

如果后面 CubeMX 新增了一个 `huartX`，推荐流程是：

1. 在 `bsp_uart.h` 增加一个枚举，比如 `BSP_UART5`。
2. 在 `bsp.c` 里绑定：

```c
(void)bsp_uart_bind(BSP_UART5, &huart5, 1U, 0U);
```

3. 在业务 app 文件里定义静态资源：

```c
static app_serial_ldc_t g_uart5_serial;
static ULONG g_uart5_tx_queue_storage[512];
static UCHAR g_uart5_thread_stack[1024];
static uint8_t g_uart5_rx_dma[128] __ALIGNED(32);
static uint8_t g_uart5_tx_chunk[64];
static uint8_t g_uart5_ldc_ring[512 + 1U];
static ldc_packet_t g_uart5_ldc_packets[8];
static uint8_t g_uart5_frame[256];
```

4. 调用 `app_serial_ldc_init()`：

```c
static UINT init_uart5_serial(void)
{
    app_serial_ldc_config_t config;

    config.name = "uart5";
    config.uart_port = BSP_UART5;
    config.ldc_config = app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);
    config.rx_dma = g_uart5_rx_dma;
    config.rx_dma_size = sizeof(g_uart5_rx_dma);
    config.tx_queue_storage = g_uart5_tx_queue_storage;
    config.tx_queue_depth = 512U;
    config.tx_chunk = g_uart5_tx_chunk;
    config.tx_chunk_size = sizeof(g_uart5_tx_chunk);
    config.thread_stack = g_uart5_thread_stack;
    config.thread_stack_size = sizeof(g_uart5_thread_stack);
    config.thread_priority = 14U;
    config.ldc_ring = g_uart5_ldc_ring;
    config.ldc_ring_size = sizeof(g_uart5_ldc_ring);
    config.ldc_packets = g_uart5_ldc_packets;
    config.ldc_packet_count = 8U;
    config.frame_buffer = g_uart5_frame;
    config.frame_buffer_size = sizeof(g_uart5_frame);
    config.flags = 0U;
    config.frame_cb = uart5_on_frame;
    config.frame_arg = NULL;

    return app_serial_ldc_init(&g_uart5_serial, &config);
}
```

## 清理掉的实验路径

这次清理删除了这些实验性文件：

```text
user/app/app_uart4_ldc_endpoint_example.c
user/app/app_uart4_ldc_endpoint_example.h
user/app/app_uart4_ldc_channel_example.c
user/app/app_uart4_ldc_channel_example.h
user/comm/ldc_channel/threadx/ldc_uart_channel_threadx.c
user/comm/ldc_channel/threadx/ldc_uart_channel_threadx.h
```

同时从 Keil 工程中移除了这些文件和 `..\user\comm\ldc_channel\threadx` include path。

`Core/Src/app_threadx.c` 也恢复为固定启动正式业务：

```c
app_ap6212_bridge_init_status = app_ap6212_bridge_init();
if(app_ap6212_bridge_init_status == TX_SUCCESS)
{
  (void)app_ap6212_sdio_probe_init();
}
```

## 后续建议

短期不要再新加 UART/LDC 层。先把 `app_serial_ldc` 作为唯一推荐入口用稳定。

如果后面觉得 `app_serial_ldc_config_t` 字段太多，可以优化它的结构，但仍然在 `app_serial_ldc` 这一层做，不再新增 `channel` 层。
