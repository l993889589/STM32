# Message Bus 方案评估

## 结论

LDC 不改。LDC 继续作为每条 UART/USB 链路的轻量 ingress/framing 内核：负责字节/块接收、ring buffer、packet descriptor FIFO、分帧和统计。

Message Bus 放在 LDC 上层，只负责业务事件路由。默认关闭：

```c
#define APP_ENABLE_MSG_BUS 0U
```

设备少时保持当前直连模式，效率最高、路径最短。设备多、模块依赖开始变复杂时，把宏改成 `1U`，再逐步把服务事件接入总线。

## 效率评估

当前直连路径：

```text
UART/USB -> LDC endpoint -> service task -> parser/state machine
```

可选总线路径：

```text
UART/USB -> LDC endpoint -> publish small message -> bus dispatcher -> handler
```

额外开销主要是：

- 一次固定大小 `app_msg_t` 结构体拷贝；
- 一次静态 ring queue push/pop；
- 遍历 handler 表匹配消息类型和来源；
- 可选的中央 dispatcher task 唤醒。

不允许把大块 UART/USB payload 复制进 bus。大数据仍留在 LDC/协议缓冲区，bus 只传指针、长度、事件类型和小整数。按这个边界实现，效率影响很小；对 115200 bps UART、AT 命令、Modbus RTU 这类链路不是瓶颈。

## 设计边界

- bus 使用静态内存，无 `malloc/free`。
- bus core 不依赖 ThreadX，便于主机测试。
- 高优先级控制消息和普通消息分开排队。
- handler 必须短小，不允许长时间阻塞。
- 耗时操作保留在各服务状态机或 worker task 内。
- 日志后续应统一走 `APP_MSG_TYPE_LOG_LINE`，避免各模块直接写 CDC。

## 建议迁移顺序

1. 引入 bus core 和 host test，默认关闭。
2. 打开宏后只初始化 bus，不改变现有数据链路。
3. 先接日志、shell/status 这类低风险事件。
4. 再接控制事件，例如 MQTT reconnect、NearLink role apply。
5. 最后再评估是否把 RS485/W800/NearLink 的 frame-ready 事件统一发布到 bus。

这个顺序可以避免一次性把稳定链路改坏，也能保证随时回退到 `codex/pre-msg-bus-baseline`。
