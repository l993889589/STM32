# Message Bus 当前状态和使用边界

## 结论

Message Bus 是可选能力，不是默认通信链路。

默认配置：

```c
#define APP_ENABLE_MSG_BUS 0U
```

设备少、链路简单时，继续使用当前直连路径：

```text
UART/USB -> LDC endpoint -> service task -> parser/state machine
```

设备多、状态交叉、日志/控制/状态通知开始混乱时，启用可选路径：

```text
service/event bridge -> Message Bus -> subscriber handler
```

## 和 LDC 的关系

LDC 负责 ingress/framing：

- 字节输入。
- 块输入。
- ring buffer。
- packet descriptor FIFO。
- 分帧。
- 溢出/drop/峰值统计。

Message Bus 负责业务事件路由：

- link activity。
- frame ready 元数据。
- service state。
- error code。
- shell/status/log 这类轻量事件。

两者不合并到 LDC core。LDC core 要保持小、快、可移植。

## 效率边界

Message Bus 的额外成本主要是：

- 一次固定大小 `app_msg_t` 拷贝。
- 一次静态 ring queue push/pop。
- 一次 subscriber 表匹配。

只要 bus 不承载大 payload，对 115200 bps UART、AT 命令、Modbus RTU、NearLink 配置这类链路影响很小。真正的性能风险不是 bus 本身，而是 handler 里阻塞、格式化长日志、复制大数据。

## 约束

- 不使用 heap。
- shared bus core 不直接调用 ThreadX/HAL。
- ISR 和 task 之间的并发保护放在平台服务包装层。
- handler 必须短小，不允许长时间阻塞。
- 大 payload 只能传指针和长度，并明确生命周期。
- 日志后续应统一为低优先级 log 事件，避免各模块直接写 CDC。

## 当前实现位置

```text
Message Bus removed from active H563 app
  app_msg_bus.c
  app_msg_bus.h

STM32H563_App/user/app/
  app_msg_bus_service.*
  app_event_bridge.*
```

## 当前策略

- `APP_ENABLE_MSG_BUS=0`：保持原链路，最小风险。
- `APP_ENABLE_MSG_BUS=1`：初始化 bus、桥接轻量事件、允许 shell 查询 bus 状态。
- 不把 RS485/W800/NearLink 的协议 payload 强行搬到 bus。
- 不用 bus 替代每个设备自己的状态机。

## 后续路线

1. 保持当前 v1.1：静态队列、full policy、stats。
2. 补足 handler 耗时约束和更多测试。
3. 如果 subscriber 数量明显增加，再评估 type index。
4. 如果 handler 阻塞成为问题，再引入异步 worker，而不是提前复杂化。
