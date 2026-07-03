# BSP / APP 分层整理说明

## 目标

当前工程把 USART3 从 NearLink 改为 Console 后，通信链路需要重新明确边界：

- BSP 负责硬件能力。
- `bsp_board` 负责本板硬件映射。
- APP 负责业务、协议和任务。
- LDC 负责字节/块接收后的缓存、分帧和包队列。

这条边界的核心原则是：`bsp_uart_bind()` 可以在 BSP/board 层做，`ldc_queue_init()` 不放进 BSP。

## 当前板级 UART 映射

板级映射集中在：

```text
STM32H563_App/user/bsp/bsp_board.c
```

当前绑定：

| 逻辑端口 | STM32 UART | 用途 | RX 模式 |
| --- | --- | --- | --- |
| `BSP_UART_W800_AT` | `huart1` | W800 AT / MQTT | DMA + idle block |
| `BSP_UART_RS485` | `huart2` | RS485 / Modbus RTU | byte interrupt |
| `BSP_UART_DEBUG` | `huart3` | Console / Debug shell | byte interrupt |

如果以后换板子，只改 `bsp_board.c` 中的映射表，不改 `app_console.c`、`app_w800.c`、`app_rs485.c`。

## 文件职责

### `bsp_uart.c/h`

只做 UART 驱动适配：

- 绑定 `UART_HandleTypeDef`
- 启动 RX
- TX write
- RX callback 分发
- 暴露只读能力接口：`bsp_uart_rx_uses_dma()`

不要在这里写具体业务，例如 shell、AT、Modbus、LDC queue。

### `bsp_board.c/h`

只做“这块板子怎么接线”的映射：

- W800 接哪个 UART
- RS485 接哪个 UART
- Console 接哪个 UART
- 哪个 UART 使用 DMA
- SPI NOR 绑定哪个 SPI

对上层提供：

```c
int bsp_board_init(void);
bsp_uart_port_t bsp_board_console_uart(void);
```

### `bsp.c/h`

保留为 BSP 总入口：

- 调用 `bsp_board_init()`
- 初始化 DWT、timer、uart、LED、LCD、touch
- 提供少量通用板级辅助函数，例如 LED、W800 reset、SPI NOR ID 打印

它不再直接写 UART 映射表。

### `app_console.c`

负责 Console 业务：

- 初始化 LDC queue
- 根据 `bsp_board_console_uart()` 选择 UART
- 根据 `bsp_uart_rx_uses_dma()` 自动选择 `LDC_QUEUE_UART_RX_DMA_BLOCK` 或 `LDC_QUEUE_UART_RX_BYTE_IT`
- 启动 console 任务

Console 不关心底层具体是 USART1/2/3。

### `app_ldc_config.c`

目前只保留 LDC 分帧策略：

- `max_frame`
- `timeout_ms`
- `delimiter`

不要把它当成硬件配置源。真实 UART 映射和 DMA 能力以 `bsp_board.c` + `bsp_uart.c` 为准。

## 为什么不在 BSP 里直接绑定 LDC

不建议在 `bsp_init()` 或 `bsp_board_init()` 里直接创建 LDC queue，原因：

1. BSP 会反向依赖通信内核。
2. BSP 会被迫知道 APP buffer、ThreadX semaphore/timer、shell 行为。
3. 裸机复用 BSP 会变困难。
4. 换业务时必须动 BSP，长期会变成调试地狱。

正确链路应该是：

```text
CubeMX/HAL UART
    ↓
bsp_uart：硬件收发能力
    ↓
bsp_board：本板端口映射
    ↓
app_console/app_w800/app_rs485：业务入口
    ↓
ldc_queue：缓存、分帧、包队列
    ↓
shell / AT / Modbus
```

## NearLink 当前状态

NearLink 已从当前应用层移除：

- USART3 改为 `BSP_UART_DEBUG`
- Console 使用 USART3
- UI 中 NearLink 卡片改为 Console
- NearLink app 和 AT module 从当前工程移除

如果后续重新接 NearLink，建议作为新的独立应用模块加入，不要复用 Console 的 UART 逻辑端口。

## 新增串口设备的步骤

1. 在 `bsp_uart_port_t` 中新增逻辑端口。
2. 在 `bsp_board.c` 映射到具体 `huartx`，设置是否 DMA。
3. 在对应 `app_xxx.c` 中创建 LDC queue 或协议状态机。
4. APP 通过逻辑端口绑定 LDC，不直接依赖 `huartx`。
5. 如需 UI/shell 状态，再从 APP 暴露只读 status API。

这能保证换板子时主要改 `bsp_board.c`，换业务时主要改 `app_xxx.c`。
