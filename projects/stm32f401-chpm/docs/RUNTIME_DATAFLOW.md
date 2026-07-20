# ThreadX、IRQ 与数据流

## 线程

所有线程使用静态对象和 2 KiB 静态栈，不使用业务堆分配。

| 线程 | 优先级 | 阻塞/周期 | 职责 |
|---|---:|---|---|
| app start | 2 | 1 s | 板级启动、参数加载、创建其余线程、健康轮询 |
| comm | 3 | 1 ms | 推进 DWIN 残帧恢复计时和应用 1 ms 服务 |
| ld_modbus server | 3 | 最长 100 ms 等帧 | 唯一 Modbus 协议/寄存器所有者，校验、异常响应和业务写入 |
| DWIN LDC owner | 3 | event flags 阻塞 | 消费 DMA 块、按 `5A A5 LEN` 拆帧；分发异步帧或完成串行请求 |
| parameter flash owner | 4 | 50 ms 有界等待 | 唯一运行期 W25Q64 参数擦写者；提交后发布结果，并在空闲期预擦备用扇区 |
| display output | 4 | 队列阻塞 | 消费有显式长度的 DWIN 输出槽；复位命令发送后执行系统复位 |
| monitor | 5 | 20 ms | 风扇、心跳、状态 LED、温度/报警状态机 |
| sensors | 6 | 50 ms | 串行推进 AHT20 与 DS18B20 采样状态机 |
| USB RX | 7 | USBX 阻塞读 | CDC 数据送入有界混合协议解析器 |

## Modbus 接收链

`USART1/DMA IRQ → HAL ReceiveToIdle 回调（逐字节插值时间戳）→ ld_modbus_rtu_framer → ThreadX event → ld_modbus server 线程 → 地址/功能码/写范围策略 → ld_modbus codec/server → 参数候选快照 → parameter flash owner → UART1 响应`

- ISR/DMA 回调只向 strict RTU framer 逐字节提交时间戳，不解析 PDU、不访问业务寄存器、不写 Flash。
- UART IDLE 只帮助估算末字节时间，不直接切帧；`ld_modbus_rtu_framer` 按协议 T1.5/T3.5 判定损坏帧和完整帧。115200 baud 时 T3.5 使用规范固定 1.75 ms。
- ISR 与 server 线程只共享 framer 的 active/ready 双缓冲；ThreadX event 只负责唤醒，不再复制第二份字节流，也不再经过通用 LDC、manual flush 或 `ld_modbus_ldc` adapter。
- server 线程只有在 DMA 位置表明没有待交付字节时才推进协议时钟，避免先越过回调尚未提交的字节时间戳。
- 线程先做 CRC、单元地址和写策略检查。标准广播地址 0 可应用但不响应；旧私有 0xF4 地址作为兼容别名映射到当前地址。
- 仅 holding 0..2 可写：从站地址、风扇模式、手动 PWM；其余实时寄存器只读。非法地址和值返回标准异常。
- holding `0x0002` 始终表示持久化的手动 PWM 预设。自动模式下写入会保存预设，但不会改变当前自动输出。
- 参数写入先组成完整候选快照，再由 Flash owner 提交。只有回读与 commit 验证成功才发布运行态和 PWM。100 ms 内完成回正常响应；确定失败返回 `0x04`；已接收但仍在处理返回 `0x05`；owner 或备用扇区预擦除忙返回 `0x06`。
- `0x05` 之后 owner 继续拥有请求槽，完成后才允许下一次写；因此不会出现超时调用者覆盖仍在使用的静态快照。地址切换请求的延迟异常帧回显原请求单元地址。
- 旧线圈 0 的 PB1 继电器动作默认返回 Server Device Failure，因为本版原理图把 PB1 定义为第二路 1-Wire，不能安全驱动。
- 调试 `printf` 的 `fputc` 是无输出 sink，不会向 USART1 注入文本。

## USB 与 DWIN

USBX CDC 线程直接获得有长度的数据块，再送入 512 字节有界解析器。解析器只识别完整的 `AB CD` 控制帧或 `5A A5` DWIN 转发帧，残帧留到下次，非法前缀逐字节重同步。USBX 自身已经提供线程化传输，因此没有把 LDC 强行扩展成通用 USB 框架。

USART2 保持 128 字节循环 ReceiveToIdle DMA。DMA 回调只把数据复制到 4 槽静态块队列并设置 ThreadX event，不在中断中解析协议。DWIN owner 按 `5A A5 LEN` 连续重组，达到 `3 + LEN` 时立即得到一个精确帧；硬件 IDLE 和 20 ms 软件静默只丢弃残帧并帮助重同步，不再充当正常帧边界。

`DWIN DMA IRQ → 4 槽静态块队列 → ThreadX event → 5A A5 LEN 解析器 → LDC 2 × 258 B → 异步业务 handler / 请求 ACK event`

解析器支持 DMA 半包、多个帧粘在一个 DMA 段、噪声前缀和最大 258 字节 DGUS 帧。每个精确帧事务式写入 LDC 2.0.2 的 258 字节静态槽并立即由 owner 读取，因此 ACK 和紧随其后的异步帧不会再合并。阻塞请求通过 mutex 串行化，再等待 owner 发布 ACK。输出策略由唯一 TX owner 负责：动态值 latest-wins，普通事件使用静态 FIFO，蜂鸣器独立可靠调度。

USBX CDC 传输块直接进入业务解析器，不使用 LDC；应用消息使用 ThreadX queue。没有业务 handler 的 debug/msg LDC 通道已删除。

## 共享资源

- W25Q64 保存由参数互斥量和 Modbus/USB 参数入口串行化；运行期只有 `parameter flash owner` 执行擦写。
- W25Q64 BUSY 轮询每次让出 1 tick；参数日志按启动时缓存的空地址以 64 字节记录追加，运行期不扫描扇区。轮换后由 owner 空闲预擦旧扇区，Modbus 提交路径不擦扇区。
- 参数保存健康快照提供成功、失败、忙、同步超时、备用扇区预擦成功/失败计数和最后结果。
- 应用状态、参数和 DWIN 输出分别使用独立 ThreadX mutex。
- USBX 使用 10 KiB 静态 byte pool；没有运行时跨线程借用栈帧。

## 传感器周期与相互影响

AHT20 使用 PB6/PB7 软件 I²C，DS18B20 使用 PB0 1-Wire。两个设备不共享
物理总线，并且只由 `app_sensor_entry` 一个线程访问，因此不会发生并发总线争用。

当前调度是串行、分阶段的：

1. AHT20 到期后发起转换，传感器线程休眠 100 ms，再读取温湿度。
2. DS18B20 到期后发起转换，线程继续运行其他传感器状态；750 ms 后读取 scratchpad。
3. 主循环每 50 ms 检查一次 deadline。

`TX_TIMER_TICKS_PER_SECOND` 为 1000，所以代码中的 tick 与毫秒一一对应。
`tx_thread_sleep()` 只挂起 sensors 线程，不会阻塞 USB、Modbus 或 DWIN 线程。

当前下一次 deadline 是在一次读取完成后设置为“当前时刻 + 10 s”，因此实际完成
周期并非严格 10 s：

- AHT20 约为 10.1 s，加上少量总线和调度开销。
- DS18B20 约为 10.75 s，加上少量总线和调度开销。

两个周期会缓慢漂移。若两者恰好同时到期，代码先处理 AHT20，它的 100 ms 等待
可能使 DS18B20 的启动或读取推迟最多约 100 ms。DS18B20 已完成的转换结果保留在
scratchpad 中，这种延迟不会破坏样本。若产品需要严格固定 10 s 周期，应改为
绝对 deadline（`next += period`）的两个非阻塞状态机，无需拆成两个线程。
