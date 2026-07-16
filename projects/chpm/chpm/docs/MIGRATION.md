# 迁移说明与行为差异

## 核心替换

| 旧工程 | 目标工程 | 迁移理由 |
|---|---|---|
| FreeRTOS/CMSIS-RTOS2 | ThreadX 原生 API | 静态线程、mutex、event flags 和有长度队列；删除未创建 EventGroup 的直接调用 |
| STM32 USB Device CDC | USBX CDC ACM | 保持真实 CDC 枚举和业务帧，统一到 ThreadX 运行时 |
| 五套 liqueue | DWIN 使用 LDC 2.0.2；消息使用 ThreadX；USB 直接解析 | 按传输语义选工具，不把分帧器当通用消息队列；一通道只有一个消费 owner |
| 旧手写 Modbus | `ld_modbus_rtu_framer` + `ld_modbus` server | 专用 T1.5/T3.5、标准 CRC/异常/广播/范围校验和无 malloc 静态映射 |
| W25Q64 地址 0 原始结构覆盖 | 版本化 CRC32 双槽提交 | 掉电原子性、回退、迁移和有界错误恢复 |

FreeRTOS 和旧 STM32 USB Device 源码已从目标目录删除。`libmodbus` 没有进入目标工程；迁移草稿中可选的同 UART client 线程也被删除，USART1 只有一个协议所有者。

LDC 固定使用 GitHub canonical 仓库的 2.0.2 提交 `d795674b47a760f02e8f253c1530b41d2d83c22f`。新接口的整块写入具有事务语义，DWIN 在 UART IDLE 或应用拥有的 20 ms 静默策略处显式提交，任务侧使用锁外复制的 `ldc_frame_read()`；完整约束见 `user/ldc/PROVENANCE.md`。

## 保持的业务

- USB 仍是 Full-Speed CDC ACM；VID:PID 为 0483:5740，manufacturer 为 STMicroelectronics，product 为 STM32 Virtual ComPort，串号继续由 MCU UID 生成。
- CDC 数据仍处理 `AB CD` 初始化、心跳、报警、状态/环境请求和 `5A A5` 电脑指标、时间、事件表、复位转发帧。
- DWIN 仍使用 USART2 115200；Modbus 仍使用 USART1 115200。
- Modbus holding 0x0000..0x0018 的地址和含义保留；标准 0x01/0x03/0x05/0x06 可由核心处理，写寄存器限制在 0..2。
- 自动/手动风扇阈值、AHT20/DS18B20、状态 LED、报警颜色、磁盘状态和页面轮换保留。
- 旧参数版本 0x00000101 可从 W25Q64 地址 0 一次性迁移。

## 有意修正

- 旧输出队列创建为 256 字节 item，却经常从 11/13/17 字节栈数组入队，造成越界读取。新队列使用显式 `length + static slot`。
- 旧混合 USB 解析器可能在残帧上读取 `i+1..i+3`。新解析器先确认帧头和完整长度。
- 旧 DS18B20 启动转换后立即读取，可能得到旧值或 85 °C。新状态机等待 750 ms 并校验 scratchpad CRC8。
- 旧 PWM 的 `.ioc` TIM3/PA6 与 Flash MISO 冲突；实际业务改为唯一的 TIM1_CH1/PA8，并修复首次空句柄 DeInit、时钟硬编码和 ARR 占空比误差。
- 旧 USART1 同时输出日志和 Modbus。目标固件的标准输出不连接物理 UART，避免污染 RTU。
- 旧迁移草稿把 Modbus 依次经过通用 LDC、线程复制、第二级 LDC adapter；现由 strict RTU framer 双缓冲直接交给唯一 server owner，删除重复分帧和字节流复制。
- 旧 DWIN 异步 handler 与阻塞读取会竞争同一个队列。现只有 DWIN owner 出队，再分别发布异步事件或完成串行 ACK 请求。
- 标准 Modbus 广播 0 现在被正确处理；旧 0xF4 特殊地址保留为兼容别名。
- PB1 在本版原理图是第二路 1-Wire，而旧代码把它当继电器。默认不执行 PB1 远程脉冲并返回设备失败；只有确认另一板版确实有继电器后才可通过编译配置恢复。

## 仍需实机确认

见 `BUILD_AND_VALIDATION.md`。特别是外接 RS485 收发器/方向控制、PB14/PB15 用途、PB1 板版差异、USB Windows 驱动重枚举和 Flash 掉电注入，不能由无硬件编译替代。
