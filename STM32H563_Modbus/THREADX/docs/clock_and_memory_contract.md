# STM32H563 时钟与内存契约

## 1. 目的

本文约束系统时钟、外设时钟查询、时间基准、Flash/RAM 布局、Cache 和 DMA 内存策略。任何 BSP 驱动不得自行猜测时钟或使用未经声明的内存区域。

第一阶段以当前 `D:/Embedded/H5/STM32H563_App` 的可运行配置为行为参考，但不依赖其 CubeMX 生成源码。

## 2. 板族时钟事实和策略

### 2.1 已确认硬件

| 时钟源 | 当前板绑定 | 频率 | 第一阶段策略 |
| --- | --- | --- | --- |
| HSE | PH0/PH1 晶振 | 25 MHz | 必需，主系统时钟源 |
| LSE | PC14/PC15 晶振 | 32.768 kHz | 当前板存在，第一阶段不要求启用 |
| HSI48 | MCU 内部 | 48 MHz nominal | USB target 启用时使用 |

用户后续板卡按“均带相同外部晶振”规划，当前契约将 25 MHz HSE 定义为该板族默认约束。仍应在每块新板的 `board_clock_config` 中显式声明 `hse_frequency_hz`，不得在各驱动中散落常量。

LSE 是否也是未来所有板必配尚未确认，因此不能提升为跨板强制条件。

### 2.2 默认性能档位

当前默认 profile：

```text
hse_frequency_hz = 25,000,000
pll1_m           = 2
pll1_n           = 40
pll1_p           = 2
pll1_q           = 2
pll1_r           = 2
pll1_fraction    = 0
sysclk_hz        = 250,000,000
hclk_divider     = 1
pclk1_divider    = 1
pclk2_divider    = 1
pclk3_divider    = 1
flash_latency    = 5
voltage_scale    = scale_0
flash_program_delay = 2
```

计算：

```text
pll1_input = 25 MHz / 2 = 12.5 MHz
pll1_vco   = 12.5 MHz * 40 = 500 MHz
pll1_p/q/r = 500 MHz / 2 = 250 MHz
sysclk     = 250 MHz
```

PLL input range、VCO range、最大 SYSCLK、Flash latency 和 voltage scale 必须在实现时依据 STM32H563 当前官方数据手册/参考手册再次验证，并形成注释中的资料版本记录。

## 3. 时钟初始化顺序

`mcu_clock` 是 RCC、PLL 和系统总线时钟的唯一 owner。初始化顺序固定为：

1. 捕获复位时的 RCC/PWR 状态；
2. 建立不依赖精确时钟的最早期安全 GPIO；
3. 设置 PWR voltage scale 0，并以 timeout 等待 ready；
4. 启动 25 MHz HSE，并以 timeout 等待 ready；
5. 配置 PLL1 参数并以 timeout 等待 lock；
6. 配置 Flash latency；
7. 切换 SYSCLK 到 PLL1；
8. 配置 AHB/APB 分频；
9. 更新 `SystemCoreClock` 和内部 clock snapshot；
10. 配置 Flash programming delay；
11. 初始化 Cache/内存属性；
12. 配置独立 HAL timebase 和单调时间；
13. 对 clock tree 做运行时一致性检查。

所有 ready/lock/switch 等待必须有硬件计数或早期有界循环。早期时钟初始化不能依赖尚未建立的 `HAL_Delay()`。

发生 HSE/PLL/clock switch 失败时：

- 记录 `error_stage` 和具体 clock error；
- 将受控输出保持安全状态；
- 不静默退回另一个主时钟并继续作为正常模式；
- 如果未来需要降级 profile，必须作为显式 target policy 并报告 `BSP_STATUS_DEGRADED`。

## 4. USB 48 MHz 时钟

USB 不是第一阶段功能，但时钟契约预留如下：

- 仅在启用 USB target feature 时启动 HSI48；
- 启用 CRS APB clock；
- CRS 同步源为 USB SOF；
- sync frequency 为 1 kHz；
- reload 由 `48,000,000 / 1,000` 计算；
- error limit 和 calibration 初值以当前可运行配置 `34`、`32` 为参考，启用 USB 前重新依据 HAL/参考手册验证；
- USB 尚未枚举、没有 SOF 时必须具有可诊断状态，不能把 CRS 未同步误报为主时钟故障。

裸机 bring-up target 不启用 USB 时，不应为方便而无条件开启 HSI48/CRS。

## 5. 外设时钟查询服务

驱动只能通过 clock query API 获取实际 kernel clock，不得使用 `SystemCoreClock / 2`、固定 250 MHz 或由调用者手工传入定时器时钟。

clock service 至少提供：

- 当前 SYSCLK、HCLK、PCLK1、PCLK2、PCLK3；
- 指定 UART/SPI/I2C/FDCAN kernel clock；
- 指定 TIM 实例的实际 timer kernel clock；
- 当前 profile 和 clock tree 诊断；
- 请求时钟与实际时钟的差异/不可用状态。

定时器查询必须处理：

- APB prescaler 对 timer clock 的倍频规则；
- STM32H5 的 timer clock source/multiplier 配置；
- 不同 timer 位宽；
- target 对 timer instance 的实际绑定。

PWM、baud、SPI、I2C 和 FDCAN timing 都必须从该服务计算并返回 achieved value/error。

## 6. 时间基准

### 6.1 HAL timebase

两份独立工程分别处理 HAL timebase：裸机工程按用户要求使用 SysTick；ThreadX 工程由内核占用 SysTick，因此仅在该工程内使用 TIM17 提供 HAL 1 ms tick。

约束：

- 裸机不得额外占用 TIM17 作为 HAL tick；
- ThreadX 工程中的 TIM17 为 `mcu_hal_timebase` 独占资源；
- 计数器和 prescaler 从实际 TIM17 kernel clock 计算；
- timebase 初始化和 clock reconfigure 后重新验证；
- TIM17 IRQ 只更新 HAL tick/单调计数并发送必要的轻量通知；
- 不在 TIM17 callback 中调用产品级 LDC 全局链表或协议处理；
- tick 中断优先级必须满足 ThreadX target 规则。

### 6.2 单调时间和 deadline

- 提供 wrap-safe `uint32_t` 毫秒 deadline；
- DWT CYCCNT 提供短时微秒测量/延时，使用前验证可用性；
- 长等待不得只使用 busy wait；
- 裸机等待必须有 timeout，并在需要时推进相关 `poll/step`；
- ThreadX task-context wait 可由 OSAL sleep/event 实现，ISR 不得调用；
- LDC timeout 由 consuming service 根据 elapsed time 调用对应实例的 `ldc_easy_tick()` 或 `ldc_easy_tick_us()`。

## 7. PWM 时钟与求解契约

PWM 调用体验为：

```text
logical_role + requested_frequency_hz + requested_duty_permille
-> actual timer clock query
-> timer ownership/conflict check
-> width-aware PSC/ARR solve
-> CCR solve
-> synchronized safe update
-> achieved_frequency_hz / error_ppm / resolution
```

求解器必须：

- 使用至少 64 位中间计算；
- 在 timer 位宽范围内联合选择 PSC/ARR；
- 优先获得低频率误差，再最大化 ARR 分辨率；
- 从 `ARR + 1` 计算 CCR，并说明舍入策略；
- 显式处理 0% 和 100%；
- 检查同一 timer 其他 channel 的 base frequency owner；
- 不因修改一个 channel 而无条件重初始化整个 timer；
- 返回实际频率、实际 duty 和误差，不静默裁剪。

PB11/TIM2_CH4 背光是第一阶段实机验证对象。

## 8. Vendor 和启动文件

- 固定使用项目内 vendor 目录，不从 CubeMX 工程目录隐式引用文件；
- 第一阶段 HAL 基线为 STM32H5 HAL 1.5.1；
- CMSIS/device/startup 版本必须与 HAL pack 兼容并记录来源；
- vendor 文件保持上游命名和内容，项目适配写在 BSP 层；
- `system_stm32h5xx.c`、启动汇编和 vector ownership 必须纳入明确 target；
- 构建不依赖 `.ioc`；
- 项目自有代码不定义 `MX_*` 公共符号。

## 9. Flash 内存 profile

内存布局属于 target/build 契约，不属于 board pin binding。

### 9.1 `standalone`

用途：新 BSP 最小裸机/RTOS bring-up，无 bootloader。

```text
vector/flash origin = 0x08000000
flash length        = 由 STM32H563RI 实际容量定义
```

### 9.2 `boot_app`

用途：兼容当前应用预留的 bootloader 区域。

当前 Keil 工程证据：

```text
application origin = 0x08020000
device flash end   = 0x081fffff（旧 target 设置）
reserved prefix    = 0x00020000（128 KiB）
```

该布局在 bootloader 协议、向量跳转、擦除粒度和实际芯片容量确认前标记为 `provisional`。启用时必须：

- 链接地址从 `0x08020000` 开始；
- 启动早期正确设置 VTOR；
- 禁止应用擦写 bootloader 保留区；
- 导出应用镜像边界供 OTA/bootloader 使用；
- 将 slot/header/CRC/签名布局留给后续 boot contract，不放进 BSP。

第一阶段必须至少完成 `standalone` 构建；`boot_app` 可先完成链接和静态验证，不宣称 bootloader 实机链路完成。

## 10. RAM、静态分配和链接 section

当前旧 Keil target 把 RAM 视为：

```text
0x20000000 - 0x2009ffff
```

新链接文件必须以 STM32H563RI 官方 memory map 为准重新确认，而不是直接复制 IDE 字符串。

第一阶段定义以下逻辑 section：

| section | 用途 | 初始化 | 约束 |
| --- | --- | --- | --- |
| `.text/.rodata` | 代码和只读数据 | Flash | 正常链接 |
| `.data/.bss` | 普通可变数据 | startup 初始化 | 不放 DMA 特殊对象 |
| `.dma_buffer` | UART/SPI DMA buffer | 静态清零或显式初始化 | 至少 32-byte 对齐；策略见下一节 |
| `.no_init` | 软复位保留诊断 | startup 不清零 | 以 magic/version/length/CRC 验证 |
| `.fault_record` | fault snapshot | startup 不清零或独立保留 | 固定结构版本和 CRC |
| task stacks | ThreadX 静态栈 | 静态 | 记录 high-water/stack error |

规则：

- BSP、LDC、基础驱动全部使用静态 context、ring、packet pool 和 buffer；
- 运行期禁止 heap；
- ThreadX objects/stacks 由 target/integration 静态持有，不泄漏到 BSP；
- 每个 buffer 的 producer、consumer、lifetime 和最大长度必须记录；
- 编译链接输出必须报告 Flash/RAM/stack 使用量。

## 11. Cache 与 DMA 一致性

当前参考工程启用：

- ICACHE：默认 2-way set associative；
- DCACHE1：read burst wrap；
- UART/SPI DMA 路径存在显式 `HAL_DCACHE_CleanByAddr`/`InvalidateByAddr` 使用。

新 BSP 统一执行以下策略：

1. ICACHE/DCACHE 初始化由 `mcu_memory` 唯一负责；
2. DMA buffer 静态分配到 `.dma_buffer`，至少 32-byte 对齐；
3. 在实现前依据 H5 DCACHE/HAL 文档确认 cache line、地址/长度对齐要求；
4. 优先选择经验证的 non-cacheable DMA 区域；若不可用，集中实现精确 clean/invalidate；
5. CPU 与 DMA 之间每次 ownership transfer 都有明确操作；
6. RX invalidate 前必须避免丢弃同一 cache line 上 CPU 尚未写回的数据；
7. TX clean 在 DMA 启动前完成；DMA completion 后才允许复用 buffer；
8. 禁止各设备驱动各自散落 Cache 维护代码；
9. Cache/DMA 错误和 restart 进入统一诊断。

直到链接 section 和一致性测试通过，对应 DMA 路径不能标记为验收完成。

## 12. 故障和持久诊断

故障记录至少包含：

- reset cause；
- boot count；
- last error stage/status；
- HSE/PLL/clock switch 错误；
- HardFault stacked registers 和 SCB fault registers；
- DMA/UART/SPI timeout、overflow、restart 计数；
- 当前 clock profile；
- PWM achieved frequency/error；
- watchdog supervisor state。

进入 safe-stop handler 后不得关闭中断再调用依赖 tick 的 `HAL_Delay()`。故障指示使用独立有界周期或硬件计数延时，并保持所有安全输出状态。

## 13. 时钟与内存验收

必须通过：

- 运行时 clock snapshot 与配置一致；
- 使用 MCO、定时器输出或等效方法测量关键时钟；
- Debug UART baud 和 PWM 频率实测在约定误差内；
- clock/PWM solver host tests 覆盖边界和舍入；
- HSE 不起振、PLL 不锁定等故障注入能有界退出并保存诊断；
- 裸机和 ThreadX target 使用相同 clock/BSP 实现；
- 两种 Flash profile 链接边界通过 map 文件检查；
- `.dma_buffer` 地址、对齐和 Cache 策略通过 map/运行测试；
- 静态扫描无硬编码 timer clock 和 `MX_*` 依赖。
