# dshan_h563_industrial 板级资源清单

## 1. 适用范围和证据状态

本清单适用于：

- 核心板：`DshanMCU-LiteH5_SCH_V1.pdf`
- 扩展板：`100ASK_MCU-Industrial-DevKit_SCH_V1.pdf`
- 原理图日期：2024-01-30
- MCU：STM32H563RIV6，VFQFPN68，带 EP69 裸焊盘

证据优先级为：实际 PCB/BOM、原理图/网表、官方 MCU/器件资料、实测、已知可运行固件、旧代码。当前表格主要来自原理图和已知固件，量产冻结前仍需核对 PCB 版本、BOM 和实测。

状态定义：

- `verified_binding`：原理图与当前已知固件一致；实现前仍要核对官方 AF/电气限制。
- `provisional`：方向合理但 DMA、IRQ、电气或器件细节尚未冻结。
- `open_issue`：存在明确矛盾或缺失，不允许据此完成对应驱动。

实现状态与证据状态分开管理：当前裸机 BSP 已达到 `compile_complete`，表示原理图范围内的底层实现已进入 Keil 构建并通过静态检查；它不把极性、波形、器件 ID 或通信结果提升为实机已验证。

## 2. 板级角色总表

| 逻辑角色 | owner | 必需性 | MCU 绑定 | 电气/安全状态 | DMA/IRQ | 第一阶段 | 证据状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `BOARD_CLOCK_HSE` | `board_clock` | 必需 | PH0/PH1，25 MHz 晶振 | 复位模拟模式；不得作 GPIO | 无 | 初始化并测量 | `verified_binding` |
| `BOARD_CLOCK_LSE` | `board_rtc` | 可选 | PC14/PC15，32.768 kHz 晶振 | 仅由 LSE/RTC 使用，不得作 GPIO | RTC IRQ 按需 | 实现 RTC 时钟与读写 | `verified_binding` |
| `BOARD_DEBUG_SWD` | debug target | 必需 | PA13 SWDIO，PA14 SWCLK，PB3 SWO，NRST | 上电保留调试功能 | Core debug | 保持可连接 | `verified_binding` |
| `BOARD_LED_STATUS` | `board_led` | 必需 | PC12 GPIO | active low；最早期默认熄灭即输出高 | 可选，无需 IRQ | 点亮/闪烁验证 | `verified_binding` |
| `BOARD_UART_DEBUG` | `transport_debug_uart` | 必需 | USART3：PC10 TX AF7，PC11 RX AF7 | RX pull-up；TX 空闲高 | USART3 IRQ；DMA 暂不冻结 | 收发/LDC 验证 | `verified_binding` |
| `BOARD_UART_WIFI` | `wifi_transport` | 可选 | USART1：PA9 TX AF7，PA10 RX AF7 | UART 空闲高 | 旧工程 RX 使用 GPDMA1 ch0，仅作参考；USART1 IRQ | 机制实现，不做 AT/MQTT | `verified_binding` |
| `BOARD_WIFI_BOOT` | `drv_w800` | 可选 | PA8 GPIO | 极性和复位采样时序集中在 board config；默认普通启动 | 无 | 实现可配置启动模式 | `provisional` |
| `BOARD_WIFI_RESET` | `drv_w800` | 可选 | PC9 GPIO | 按参考固件配置 active low；早期保持无效高电平，驱动初始化时执行有界复位脉冲 | 无 | 实现复位时序 | `provisional` |
| `BOARD_WIFI_WAKE` | `drv_w800` | 可选 | PC8 GPIO | 按参考固件配置 active high；默认不请求唤醒 | 可选 EXTI 不启用 | 实现电平控制 | `provisional` |
| `BOARD_SPI_FLASH` | `spi_flash_bus` | 必需 | SPI1：PA5 SCK AF5，PA6 MISO AF5，PA7 MOSI AF5 | mode 0；总线初始化前保持片选无效 | 第一版有界轮询，无 DMA | 实现 ID、读、页写、4 KiB 擦除和忙等待 | `verified_binding` |
| `BOARD_FLASH_CS` | `drv_gd25lq128` | 必需 | PA4 GPIO | active low；最早期先输出高 | 无 | JEDEC ID | `verified_binding` |
| `BOARD_USB_FS` | `usb_device_service` | 可选 | PA11 DM AF10，PA12 DP AF10 | HSI48 USB kernel clock；device-only PCD 边界 | USB_DRD_FS IRQ，优先级 8 | 实现 PCD 初始化、启停与事件转发 | `verified_binding` |
| `BOARD_USB_ID_LOGIC` | `usb_device_service` | 可选 | PB13 GPIO input，连接 Type-C CC/板级电源逻辑 | 禁止驱动；无上下拉读取 | 第一版不启用 EXTI | 实现只读状态 | `provisional` |
| `BOARD_FDCAN_FIELD_1` | `fdcan_service_1` | 可选 | FDCAN1：PB7 TX AF9，PE0 RX AF9；TJA1042 | STB 由 4.7 kOhm 下拉固定正常模式；split termination 约 120 ohm | IT0/IT1，优先级 8；静态 message RAM | 实现自动 nominal/data timing、收发和 bus-off 诊断 | `verified_binding` |
| `BOARD_FDCAN_FIELD_2` | `fdcan_service_2` | 可选 | FDCAN2：PB6 TX AF9，PB12 RX AF9；TJA1042 | 同上 | IT0/IT1，优先级 8；静态 message RAM | 同 FDCAN1 | `verified_binding` |
| `BOARD_UART_RS485_1` | `rs485_transport_1` | 可选 | USART2：PA2 TX AF7，PA3 RX AF7；MAX13487 | 自动方向，无 DE GPIO；UART 空闲高 | USART2 IRQ；DMA 暂不冻结 | 至少一条链路验证 | `verified_binding` |
| `BOARD_UART_RS485_2` | `rs485_transport_2` | 可选 | UART4：PA0 TX AF8，PA1 RX AF8；MAX13487 | 自动方向，无 DE GPIO；UART 空闲高 | UART4 IRQ；DMA 暂不冻结 | 机制覆盖 | `verified_binding` |
| `BOARD_SPI_DISPLAY` | `spi_display_bus` | 可选 | SPI2：PB10 SCK AF5，PC1 MOSI AF5，PC2 MISO AF5 | ST7796 使用 mode 0；初始化前 CS 无效 | 第一版有界轮询，无 DMA | 实现总线与 ST7796 基础驱动 | `verified_binding` |
| `BOARD_DISPLAY_CS` | `display_device` | 可选 | PD11 GPIO | active low；最早期输出高 | 无 | 建立安全状态 | `verified_binding` |
| `BOARD_DISPLAY_DC` | `display_device` | 可选 | PD12 GPIO | 默认 data/command 状态按器件资料确定 | 无 | 建立安全状态 | `verified_binding` |
| `BOARD_DISPLAY_RESET` | `display_device` | 可选 | PB4 GPIO | active low；初始化前保持复位，完成时序后释放 | 无 | 实现有界复位时序 | `verified_binding` |
| `BOARD_PWM_LCD_BACKLIGHT` | `display_backlight` | 可选 | TIM2_CH4：PB11 AF1 | 默认关闭；极性需结合背光电路确认 | TIM2 IRQ 通常不需要；DMA 不需要 | PWM 首个实机验证 | `verified_binding` |
| `BOARD_I2C_TOUCH` | `i2c_touch_bus` | 可选 | I2C1：PB8 SCL AF4，PB9 SDA AF4 | 外部 4.7 kOhm 上拉；开漏 | 第一版有界轮询，无 DMA | 实现物理频率配置与 FT6336U 访问 | `verified_binding` |
| `BOARD_TOUCH_INTERRUPT` | `drv_ft6336` | 可选 | PB14 GPIO input | 参考器件 active low；第一版轮询读取 | 不启用 EXTI | 实现只读状态 | `provisional` |
| `BOARD_TOUCH_RESET` | `drv_ft6336` | 可选 | PB15 GPIO | 参考器件 active low；最早期保持复位 | 无 | 实现有界复位时序 | `provisional` |
| `BOARD_EXPANSION_HEADER` | `board_expansion` | 可选 | J1 暴露 PC0/3/4/5、PB0/1/2/5/8/9/11/14/15、PC6/7/10/11/13、PA15 等 | 与启用板载功能共享的引脚不得二次分配 | 无统一 IRQ/DMA | 仅提供资源查询，不创建任意 GPIO 后门 | `verified_binding` |

“可选”表示器件缺失或初始化失败可进入 degraded 状态，不表示资源可以被其他模块随意占用。

### 当前裸机 compile-only 覆盖

- 核心板：LED、四路 UART 角色、W800 boot/reset/wake、SPI1 GD25LQ128 读/页写/4 KiB 擦除、USB DRD FS device-controller、LSE RTC。
- 工业扩展板：两路自动方向 RS-485 UART、双路 FDCAN classic/FD 时序求解与静态收发、SPI2 ST7796 RGB565、TIM2_CH4 背光、I2C1 FT6336U。
- 所有时序配置对应用层使用物理量：PWM 频率/千分比、SPI/I2C/FDCAN 波特率；应用不计算 PSC/ARR/CCR、SPI 分频、I2C TIMINGR 或 CAN segment。
- 当前有界阻塞版本不启用 DMA；USB 使用外设专用 PMA；所有 BSP/driver context 与缓冲区均为静态存储。
- USB 只提供可挂接协议栈的 PCD/endpoint 边界，不内置 CDC/HID；W800 不内置 AT/MQTT；RS-485 不内置 Modbus。这些属于 BSP 上层服务，不是板级驱动缺项。
- 双路 FDCAN 的 bus-off 只在 ISR 记录，恢复由 superloop/任务显式调用 `bsp_fdcan_recover()`，避免在 ISR 中执行 stop/start。

## 3. 第一阶段资源所有权

| 可变资源 | 唯一 owner | 允许的消费者 | 禁止事项 |
| --- | --- | --- | --- |
| 系统 RCC/PLL/总线时钟 | `Core/Src/main.c` 的 `system_clock_config` | `bsp_clock_stm32h5` 只读查询 | 驱动自行改 PLL 或 APB 分频 |
| 裸机 SysTick | `Core/Src/stm32h5xx_it.c` | HAL deadline、诊断 | 外设驱动修改 SysTick 配置 |
| TIM2 base + CH4 | `bsp_pwm_stm32h5` | `display_backlight` | LCD 驱动直接修改 PSC/ARR |
| USART1/2/3/UART4 | `bsp_uart_stm32h5` | 各逻辑 transport owner | 应用拿 HAL handle 或重初始化 UART |
| UART RX 静态缓冲区 | 各 UART transport context | 对应 LDC/service | 多服务共享同一 mutable ring |
| SPI1 总线 | `spi_flash_bus` | `drv_gd25lq128` | Flash driver 重新配置共享 bus |
| PA4 Flash CS | `drv_gd25lq128` | 无其他 owner | 通用 GPIO API 任意操作 |
| SPI2 总线 | `spi_display_bus` | `drv_st7796` | LCD driver 内部调用 `MX_SPI2_Init()` 或重配 SPI2 |
| I2C1 总线 | `i2c_touch_bus` | `drv_ft6336` | Touch driver 重新初始化共享 bus |
| FDCAN1/FDCAN2 | 各 `bsp_fdcan_stm32h5` context | 对应 field service | 应用直接修改 filter/message RAM |
| USB DRD FS | `bsp_usb_device` | 上层 device/class stack adapter | 应用获取 PCD handle 或重复定义 HAL callbacks |
| RTC + backup domain | `board_rtc` | 时间服务 | 其他模块修改 LSE/RTC prescaler 或擦除 backup domain |
| IRQ vectors/HAL callbacks | 对应 `board_uart`、`board_fdcan`、`board_usb_device` 唯一 owner | 静态 owner dispatch | 各模块重复定义同一 vector/HAL callback |
| LDC context/ring/pool | consuming service | 对应 transport | BSP 全局持有所有 LDC 实例 |
| OS events/mutex | OSAL backend/integration | 明确的共享边界 | BSP 公共层直接包含 `tx_api.h` |

## 4. DMA 分配规则

当前裸机全外设第一版不启用 DMA。旧工程中的分配只作为后续性能阶段参考：

- USART1 RX 曾使用 `GPDMA1_Channel0`；
- SPI2 路径中存在 `GPDMA1_Channel7` 使用痕迹；
- USART2、USART3、UART4 当前主要使用中断 ReceiveToIdle。

后续启用 DMA 前必须完成：

1. 从 STM32H563 官方 request mapping 验证请求合法性；
2. 给每个启用 target 建立唯一 request/channel owner；
3. 明确 normal/circular/linked-list 模式；
4. 明确 half/full/IDLE 并发和 wrap 行为；
5. 明确 buffer section、对齐、Cache clean/invalidate 和所有权转换；
6. 明确错误、overflow、restart 和诊断计数器；
7. 编译期检查同一 target 的 DMA 冲突。

优先设计连续 UART RX 为 circular DMA + IDLE；若 HAL/H5 GPDMA 限制导致暂用 normal ReceiveToIdle，也必须实现有界重启和无窗口丢包诊断。

## 5. IRQ 分配规则

- SWD/SWO 保持可用，不能在早期启动阶段重用 PA13、PA14、PB3。
- 每个 vector 只定义一次，由对应板级 owner 调用 HAL handler 并分发事件；禁止另建第二套集中或分散 handler。
- ISR 只捕获 producer position、状态、时间戳和错误，并执行 `_from_isr` 通知。
- ThreadX target 的可调用 RTOS API 中断优先级必须满足 ThreadX/端口约束；具体数值在 target 配置冻结。
- 本目标只修改裸机工程：外设 IRQ 默认优先级为 FDCAN/USB 8、UART 10；Touch 第一版轮询，不启用 EXTI。
- 所有 UART ORE、DMA error、buffer overflow、restart 事件都必须计数。

## 6. 早期安全状态顺序

在切换 GPIO 为输出前先装载目标电平，再配置 mode：

1. PA4 Flash CS 置高；
2. PD11 Display CS 置高；
3. LCD 背光保持关闭；
4. W800 reset/boot/wake 按冻结后的器件时序进入安全态；
5. Display/Touch reset 按器件资料进入安全态；
6. PC12 Status LED 保持熄灭；
7. 保留 SWD/SWO 和晶振引脚；
8. 完成主时钟后才初始化高活动总线。

任何极性尚未验证的引脚，在量产绑定冻结前不得驱动可能损坏或误启动外设的电平。

## 7. 冲突集合

必须在编译或 board review 时检查：

- PB4 同时具有复位时调试相关属性和 LCD reset 用途；必须保证早期 SWD 可连接。
- TIM2 的 PSC/ARR 属于全定时器共享资源；CH4 背光请求必须与未来 TIM2 其他通道兼容。
- USART3 专用于 Debug，禁止协议服务和日志复用同一 UART。
- USART1 专用于 W800，禁止 Debug 抢占。
- MAX13487 为自动方向，禁止创建不存在的 RS-485 DE GPIO。
- 双 FDCAN 均带约 120 ohm split termination；接入总线前确认节点拓扑。
- DMA request/channel 未冻结前，不允许把旧 CubeMX 分配写入公共接口。
- Cacheable DMA buffer 未建立一致性策略前，不允许启用对应 DMA target。

## 8. 未解决问题

以下事项不阻塞编译期实现，但必须保留到硬件验收：

1. `PD13误` 已通过 PDF 内嵌网表和参考工程交叉确认：实际为 PD11 Display CS、PD12 Display DC；仍需 PCB 连通性实测。
2. PB4 作为 LCD reset 的上电和调试影响需要实机确认。
3. W800 boot/reset/wake 极性和最小时序按参考固件参数化实现，需模块资料或实测确认。
4. LCD 背光按 PB11 TIM2_CH4 active high 实现，需示波器/亮度实测确认。
5. 显示和触摸器件按参考工程的 ST7796、FT6336U（7-bit 地址 0x38）实现；装配 BOM 未提供，需读 ID/实物确认。
6. USB_ID/CC 网络按 PB13 只读输入处理，绝不主动驱动；其阈值和 OTG 行为需硬件实测。
7. 第一版明确不启用 DMA；后续性能阶段另行冻结 GPDMA request/channel 与 Cache 策略。
8. 25 MHz HSE 已冻结为板族约束；LSE 是否为未来所有板必配仍未确认。

## 9. 换板规则

新板到来时：

1. 保留当前 `dshan_h563_industrial` target 可构建；
2. 复制并修改 `board_config.h` 与 `board_*.c` 板级绑定；
3. 重新填写本资源清单，不直接复制并假定相同；
4. 重新核对 package pin、AF、电压域、boot strap、debug、晶振和安全输出；
5. 复用 `bsp_*` 公共 API、`*_stm32h5` 机制、LDC、OSAL API 和上层服务；
6. 如果仅改引脚/实例却需要修改共享层，视为架构缺陷；
7. old/new board 均执行裸机和 ThreadX 构建矩阵。
