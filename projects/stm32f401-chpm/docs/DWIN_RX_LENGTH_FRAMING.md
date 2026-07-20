# CHPM DWIN RX 长度分帧实现

更新日期：2026-07-20  
范围：USART2 DWIN 接收链路  
约束：ThreadX、全静态内存、只编译和主机测试，不烧录

## 结论

DWIN 接收继续使用 USART2 循环 DMA；发送继续使用有界阻塞
`HAL_UART_Transmit`。本次只修改接收数据交接和拆帧：

```text
USART2 circular DMA (128 B)
        |
        v
ISR: copy + event only
4 × 128 B static chunk queue
        |
        v
DWIN RX owner thread
5A A5 LEN stream parser
        |
        v
exact frame, maximum 258 B
        |
        v
LDC 2 × 258 B -> ACK / asynchronous handler
```

## 分帧规则

线路格式为：

```text
5A A5 LEN BODY...
```

完整帧长度严格等于 `3 + LEN`。`LEN` 为 8 位，所以最大线路帧是
`3 + 255 = 258` 字节。

- DMA 一次只收到半帧：保留候选，下一数据块继续拼接；
- DMA 一次收到多个帧：达到每个帧长后立即逐帧交付；
- 完整帧后跟半帧：先交付完整帧，IDLE 时只丢弃后面的残帧；
- 前缀噪声：逐字节寻找 `5A A5`；
- 连续 `5A 5A A5`：保留后一个 `5A` 作为新同步头；
- `LEN == 0`：作为非法候选丢弃并重新同步；
- UART 错误或静态块队列溢出：丢弃所有不连续数据并从新同步头恢复。

硬件 IDLE 和 20 ms 软件静默不再定义正常帧，只用于确认当前候选已经
截断。这样 ACK 与异步屏幕帧紧邻到达时不会被合并成一个 LDC 帧。

## 并发和资源边界

- DMA/HAL 回调处于中断上下文，只做最大 128 字节的有界复制和
  `tx_event_flags_set()`；
- `dwin_rx_parser`、LDC 写入、ACK 判断和异步业务回调全部在唯一
  DWIN RX owner 线程运行；
- ISR 到线程使用 4 个静态数据块，没有堆分配；
- LDC 保留为串口完整帧的事务交接层，使用 2 个 258 字节静态槽；
- 每个精确帧提交后立即读取和分发，避免同一 DMA 块中的多个短帧占满
  两个 LDC 槽；
- 静态队列满时不拼接未知缺口后的数据，而是计数、清队列并重同步。

## 诊断

`dwin_ldc_channel_get_diagnostics()` 除原有拒绝、溢出、无请求 ACK 和
异步帧计数外，还返回：

- 完整帧数；
- 丢弃字节数；
- 非法长度数；
- 截断帧数；
- 完整帧投递失败数。

## 验证

- DWIN RX parser 专项主机测试：7 类场景全部通过；
- 工程主机测试：3 + 1 + 3，共 7/7 通过；
- 静态工程检查：569 项通过；
- Keil/ARMClang 全量重编译：0 error、0 warning；
- 固件尺寸：Code 84460、RO-data 1456、RW-data 112、ZI-data 44184。

本次没有生成 HEX、没有连接调试器、没有烧录。

## 待实机确认

新板到手后重点做两组连续发送：

1. 6 字节 ACK 后无间隔紧跟异步帧；
2. 8 字节 CRC ACK 后无间隔紧跟异步帧。

期望两帧均被独立识别，当前请求只消费 ACK，异步帧进入原业务 handler；
同时观察静态块溢出、截断帧和 ACK 超时计数不增长。
