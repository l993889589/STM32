# 迁移说明与行为差异

## 核心替换

| 旧工程 | 目标工程 | 迁移理由 |
|---|---|---|
| FreeRTOS/CMSIS-RTOS2 | ThreadX 原生 API | 静态线程、mutex、event flags 和有长度队列；删除未创建 EventGroup 的直接调用 |
| STM32 USB Device CDC | USBX CDC ACM | 保持真实 CDC 枚举和业务帧，统一到 ThreadX 运行时 |
| 五套 liqueue | DWIN 使用 LDC 2.0.2；消息使用 ThreadX；USB 直接解析 | 按传输语义选工具，不把分帧器当通用消息队列；一通道只有一个消费 owner |
| 旧手写 Modbus | `ld_modbus_rtu_framer` + `ld_modbus` server | 专用 T1.5/T3.5、标准 CRC/异常/广播/范围校验和无 malloc 静态映射 |
| W25Q64 地址 0 原始结构覆盖 | 版本化 CRC32 双扇区追加日志 | 掉电原子性、回退、旧格式迁移、降低擦除频率和有界错误恢复 |

## 与 H7 RTX5 All In One 的分层对应

参考工程用于确定工程导航和职责边界，不复制 H7 的寄存器、外设实例或 RTX5 端口。CHPM 保留 STM32F401 的启动文件、HAL、链接布局与 Cortex-M4 ThreadX/AC6 端口，Keil 分组对应如下：

| H7 参考工程 | CHPM 目标工程 | 职责 |
|---|---|---|
| `User` | `Application/User/Core` | 启动入口、中断路由、ThreadX 入口与 USB 控制器组合 |
| `APP` | `APP` | 产品策略、services、shell、DWIN、静态 ThreadX 线程、通信 owner 与业务状态机 |
| `BSP` | `BSP` | STM32F401 外设机制、板级资源及 AHT20、DS18B20、DWIN、W25Qxx 器件驱动 |
| `ModbusRTU/*` | `ld_modbus` + `services/drv_modbus_port` | 可移植协议核/主从 API，以及唯一 UART/ThreadX 端口 |
| `RTX5/Source`、`RTX5/Ports` | `Middlewares/ThreadX/RTOS/ThreadX/Core` + Cortex-M4 AC6 port | RTOS 内核与架构端口；按用户要求固定为 ThreadX |

依赖方向固定为 `app -> services/protocol -> drivers -> BSP -> HAL/CMSIS`。LDC 与 ld_modbus 保持上游实现不变；ThreadX 对象、UART 实例、DMA、DWT 时间源和板级策略只出现在 CHPM 适配层，不进入两个通用库。

Keil 工程树按个人单板工程精简：项目自有功能只保留 `APP`、`BSP`、`ldc`、`ld_modbus` 四个主要分组。源码目录仍保留 `user/app`、`user/services`、`user/drivers` 等职责信息，分组合并只改善导航，不改变依赖方向和运行行为。

FreeRTOS 和旧 STM32 USB Device 源码已从目标目录删除。`libmodbus` 没有进入目标工程；迁移草稿中可选的同 UART client 线程也被删除，USART1 只有一个协议所有者。

`ld_modbus` 的 core、client、server 和 RTU framer 四个上游实现文件均编译进目标。这里的“完整库”不等于同时启动主站与从站线程：当前产品只启用 server，client API 留作未来由独立通信角色使用，禁止与 server 竞争 USART1。

LDC 固定使用 GitHub canonical 仓库的 2.0.2 提交 `d795674b47a760f02e8f253c1530b41d2d83c22f`。DWIN owner 先按 `5A A5 LEN` 得到精确帧，再事务式写入 258 字节 LDC 槽并读取分发；UART IDLE/20 ms 静默只恢复残帧，不再决定正常帧边界。完整约束见 `user/ldc/PROVENANCE.md`。

## 保持的业务

- USB 仍是 Full-Speed CDC ACM；VID:PID 为 0483:5740，manufacturer 为 STMicroelectronics，product 为 STM32 Virtual ComPort，串号继续由 MCU UID 生成。
- CDC 数据仍处理 `AB CD` 初始化、心跳、报警、状态/环境请求和 `5A A5` 电脑指标、时间、事件表、复位转发帧。
- DWIN 仍使用 USART2 115200；Modbus 仍使用 USART1 115200。
- Modbus holding 0x0000..0x0018 的地址保留；标准 0x01/0x03/0x05/0x06 可由核心处理，写寄存器限制在 0..2。`0x0002` 明确为手动 PWM 预设，自动模式写入只保存预设、不改变当前自动输出。
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
