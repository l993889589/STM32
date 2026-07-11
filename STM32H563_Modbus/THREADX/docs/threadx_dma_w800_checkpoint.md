# STM32H563 ld_modbus：动态 T3.5、ThreadX、UART DMA 与 W800 TCP

## 结论

`D:\Embedded\H5\stm32_h563_modbus_it` 现在使用同一份静态内存协议代码支持四种组合：

- 裸机 + USART2 ReceiveToIdle IT。
- 裸机 + USART2 ReceiveToIdle DMA。
- ThreadX + USART2 ReceiveToIdle IT。
- ThreadX + USART2 ReceiveToIdle DMA。

四个 Keil ArmClang 6.21 目标均为 0 error、0 warning，Load Region 与向量表都从
`0x08000000` 开始。裸机 DMA、ThreadX IT、ThreadX DMA 分别完成 COM3 的 16/16
Modbus RTU 回归；裸机 IT 保留此前的 16/16 基线。

## 硬件与时钟

- MCU：STM32H563RIV6。
- HSE：25 MHz；SYSCLK：250 MHz。
- RS485-1：USART2，PA2/PA3，115200 8N1，MAX13487 自动方向。
- DMA：GPDMA1 Channel 1 / 2 分别负责 USART2 RX / TX，IRQ 优先级 10。
- ThreadX：1 kHz SysTick；HAL 1 ms 时基改用 TIM17。
- ThreadX 任务：Modbus 优先级 10、静态栈 2048 bytes；LED 优先级 20、静态栈
  512 bytes。

## T3.5 分层

串口实际参数由 `bsp_uart_get_config()` 返回。LDC 只提供通用的
`ldc_serial_silence_us()`：按起始位、数据位、校验位和停止位换算字符静默时间，
不知道 Modbus 的存在。Modbus 应用在不高于 19200 baud 时请求 3.5 字符时间；更高
波特率使用固定 1750 us。

这样 UART 配置、通用分帧和协议策略分别属于 BSP、LDC、Modbus 应用，不互相反向依赖。

## DMA 接收

DMA 使用静态 64-byte、32-byte 对齐接收块和 512-byte 环形缓冲。HT 事件只搬运前半
段并保存偏移；后续 IDLE/TC 只搬运新增部分，避免同一字节重复进入 LDC。DMA 区域在
CPU 读取前执行 cache invalidate，同时记录 HT、IDLE、TC、overflow、error 和 restart
计数。

DMA 目标的发送也使用真实 GPDMA，而不是继续调用 polling HAL。TX 缓冲发送前执行
cache clean，完成、错误和超时分别计数；ThreadX DMA 的 16/16 从站回归及板端三步
主机回归都覆盖了 TX DMA。IT 目标保留 polling TX，使用相同上层 transport API。

## ThreadX 端口坑

第一次构建误把 TrustZone Secure Stack 可选源加入普通 Non-Secure 镜像，出现：

```text
tx_thread_secure_stack.c:282: error: call to undeclared function '__TZ_get_PSPLIM_NS'
```

随后还遇到 HardFault/UsageFault 向量重复，以及
`Image$$ARM_LIB_STACK$$ZI$$Limit` 未定义。最终规则是：

- 定义 `TX_SINGLE_MODE_NON_SECURE=1`。
- 不编译 Secure Stack 可选实现和重复的 `tx_misra.S`。
- HardFault、UsageFault、SysTick 由 AC6 ThreadX 端口所有。
- 低级初始化使用本工程链接器提供的 `Image$$RW_IRAM1$$ZI$$Limit`。
- `SYSTEM_CLOCK=250000000`，SysTick reload 按 1000 Hz 计算。

## W800 Modbus TCP

从 `STM32H563_App` 只复制静态 AT 核心和 W800 驱动到新工程，原 App 未修改，也未复制
任何 Wi-Fi 凭据。

依据联盛德 W800 SDK AT V1.1，`AT+SKCT` 支持 TCP Client/Server，Server 的
`AT+SKSTT` 会返回监听 socket 及接入的子 socket。新传输层负责：

- Client：连接远端、按 MBAP length 精确读取一帧、校验事务号与 unit id。
- Server：创建 listener、查询接入子 socket、处理完整 ADU 并发送响应。
- 主从共用 `SKSND`/`SKRCV` 二进制定长窗口。

网络代码已进入四个 Keil 目标；Server 启用分支在四目标 0/0，Client 启用分支在裸机
IT 和 ThreadX DMA 0/0。运行错误会关闭 socket、清理静态上下文并延时重连。实际
Wi-Fi/TCP 实机验收仍需要单独提供 SSID、密码和局域网测试端点。

## RTU 主机

保持寄存器 60 的 `0x4D53` 命令触发一个不阻塞的 RTU 主机验收序列：FC03 读取身份、
FC06 写 `0x55AA`、FC03 读回。裸机 IT 和 ThreadX DMA 均与 COM3 模拟从站完成三步，
只有有效响应才会推进状态，随后自动恢复板端从站。

## 构建与烧录

```powershell
cd D:\Embedded\H5\stm32_h563_modbus_it
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All
.\build_threadx.ps1 -Variant All

.\flash.ps1 -Runtime ThreadX -Variant DMA
```

RTU 回归：

```powershell
cd D:\Embedded\H5\desktop-debug-assistant
npm run test:modbus-hardware
```

测试结束时板上保留 ThreadX + DMA standalone 镜像。内部 Flash 已按用户授权整片擦除，
产品 Boot/App 需要以后重新烧录。
