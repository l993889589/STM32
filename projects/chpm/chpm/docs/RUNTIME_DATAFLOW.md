# ThreadX、IRQ 与数据流

## 线程

所有线程使用静态对象和 2 KiB 静态栈，不使用业务堆分配。

| 线程 | 优先级 | 阻塞/周期 | 职责 |
|---|---:|---|---|
| app start | 2 | 1 s | 板级启动、参数加载、创建其余线程、健康轮询 |
| comm | 3 | 1 ms | 显式推进唯一 DWIN LDC idle tick 和应用 1 ms 服务 |
| ld_modbus server | 3 | 最长 100 ms 等帧 | 唯一 Modbus 协议/寄存器所有者，校验、异常响应和业务写入 |
| DWIN LDC owner | 3 | event flags 阻塞 | 唯一 `ldc_frame_read` 调用者；分发异步帧或完成串行请求 |
| display output | 4 | 队列阻塞 | 消费有显式长度的 DWIN 输出槽；复位命令发送后执行系统复位 |
| monitor | 5 | 20 ms | 风扇、心跳、状态 LED、温度/报警状态机 |
| sensors | 6 | 50 ms | AHT20 与 DS18B20 的非连续阻塞采样状态机 |
| USB RX | 7 | USBX 阻塞读 | CDC 数据送入有界混合协议解析器 |

## Modbus 接收链

`USART1/DMA IRQ → HAL ReceiveToIdle 回调（逐字节插值时间戳）→ ld_modbus_rtu_framer → ThreadX event → ld_modbus server 线程 → 地址/功能码/写范围策略 → ld_modbus codec/server → UART1 响应`

- ISR/DMA 回调只向 strict RTU framer 逐字节提交时间戳，不解析 PDU、不访问业务寄存器、不写 Flash。
- UART IDLE 只帮助估算末字节时间，不直接切帧；`ld_modbus_rtu_framer` 按协议 T1.5/T3.5 判定损坏帧和完整帧。115200 baud 时 T3.5 使用规范固定 1.75 ms。
- ISR 与 server 线程只共享 framer 的 active/ready 双缓冲；ThreadX event 只负责唤醒，不再复制第二份字节流，也不再经过通用 LDC、manual flush 或 `ld_modbus_ldc` adapter。
- server 线程只有在 DMA 位置表明没有待交付字节时才推进协议时钟，避免先越过回调尚未提交的字节时间戳。
- 线程先做 CRC、单元地址和写策略检查。标准广播地址 0 可应用但不响应；旧私有 0xF4 地址作为兼容别名映射到当前地址。
- 仅 holding 0..2 可写：从站地址、风扇模式、手动 PWM；其余实时寄存器只读。非法地址和值返回标准异常。
- 旧线圈 0 的 PB1 继电器动作默认返回 Server Device Failure，因为本版原理图把 PB1 定义为第二路 1-Wire，不能安全驱动。
- 调试 `printf` 的 `fputc` 是无输出 sink，不会向 USART1 注入文本。

## USB 与 DWIN

USBX CDC 线程直接获得有长度的数据块，再送入 512 字节有界解析器。解析器只识别完整的 `AB CD` 控制帧或 `5A A5` DWIN 转发帧，残帧留到下次，非法前缀逐字节重同步。USBX 自身已经提供线程化传输，因此没有把 LDC 强行扩展成通用 USB 框架。

USART2 的 ReceiveToIdle DMA 数据进入工程内唯一的 DWIN LDC 通道。LDC 2.0.2 使用 2 个 256 字节静态槽和 `LDC_FULL_REJECT_NEW`；`ldc_rx_write()` 必须事务式接受完整 DMA 段，随后才允许在硬件 IDLE 或 20 ms 应用静默边界调用 `ldc_rx_idle()`。

`DWIN DMA IRQ → DWIN LDC → ThreadX event → DWIN LDC owner → 异步业务 handler / 请求 ACK event`

只有 DWIN owner 调用 `ldc_frame_read()`，帧复制发生在 LDC 临界区之外。阻塞请求通过 mutex 串行化，再等待 owner 发布 ACK；不再由 `dwin_drv` 与异步 handler 竞争消费同一帧。输出队列由 12 个 260 字节静态槽组成，队列只传槽指针；生产者在容量不足时明确失败，不再把短栈缓冲区当作 256 字节对象复制。

USBX CDC 传输块直接进入业务解析器，不使用 LDC；应用消息使用 ThreadX queue。没有业务 handler 的 debug/msg LDC 通道已删除。

## 共享资源

- W25Q64 保存由参数互斥量和 Modbus/USB 参数入口串行化。
- 应用状态、参数和 DWIN 输出分别使用独立 ThreadX mutex。
- USBX 使用 10 KiB 静态 byte pool；没有运行时跨线程借用栈帧。
