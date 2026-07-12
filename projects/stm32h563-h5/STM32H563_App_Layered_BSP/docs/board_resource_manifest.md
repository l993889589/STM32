# 板级资源清单

本文档记录当前 `STM32H563RIV6` 核心板与工业底板的已选资源。参与编译的唯一权威配置是 `user/bsp/board_resources.h`；本文档用于原理图评审、换板移植和人工检查。

## 使用边界

- 应用只使用逻辑角色，例如 `BSP_UART_W800_AT`、`BOARD_SPI_DISPLAY`、`BOARD_PWM_LCD_BACKLIGHT`，不直接保存 HAL 句柄或 GPIO/DMA 常量。
- `board_resources.h` 只描述当前 PCB 实际连接，不复制 MCU 的完整复用功能表。选新引脚时仍须查对应封装的数据手册。
- 换板只修改 `board_resources.h` 和 `board_*.c`；`mcu_*.c`、设备驱动、协议和应用原则上不跟随引脚变化。
- `.ioc` 与 `.mxproject` 仅保留为时钟种子和历史证据，不参与外设初始化，也不能覆盖板级资源文件。
- 当前只进行了静态检查与 Keil 编译，所有电气、波形、总线通信和异常恢复仍为 `hardware_unverified`。

## 芯片与时钟

| 项目 | 约束 | 所有者 | 状态 |
| --- | --- | --- | --- |
| 精确器件 | STM32H563RIV6，VFQFPN68/EP69 | Keil target / `board_resources.h` | `compile_complete` |
| HSE | 25 MHz 外部晶振 | `system_clock_config()` | `compile_complete` |
| LSE | 32.768 kHz 外部晶振 | `system_clock_config()` / RTC | `compile_complete` |
| 系统时钟 | 保留现有 CubeMX 时钟种子；`mcu_clock.c` 启动后复核 | Core + BSP | `compile_complete` |
| HAL 毫秒时基 | TIM17；ThreadX 独占 SysTick | `mcu_hal_timebase.c` | `compile_complete` |
| I/D Cache | BSP 统一初始化和维护 | `mcu_cache.c` | `compile_complete` |

## GPIO 与控制信号

| 逻辑资源 | 引脚 | 有效电平/安全状态 | 所有者 | 状态 |
| --- | --- | --- | --- | --- |
| 状态灯 | PC12 | 低有效；启动默认熄灭 | `board_gpio.c` | `compile_complete` |
| W800 BOOT | PA8 | 启动默认低 | `board_gpio.c` | `compile_complete` |
| W800 RESET | PC9 | 低有效；启动先保持复位 | `board_gpio.c` | `compile_complete` |
| W800 WAKE | PC8 | 启动默认低 | `board_gpio.c` | `compile_complete` |
| LCD CS/DC/RESET | PD11/PD12/PB4 | CS默认释放、RESET低有效 | `board_gpio.c` | `compile_complete` |
| Flash CS | PA4 | 低有效，默认释放 | `board_gpio.c` | `compile_complete` |
| Touch INT/RESET | PB14/PB15 | INT输入、RESET低有效 | `board_gpio.c` | `compile_complete` |
| USB ID | PB13 | 只读输入、无内部上下拉 | `board_gpio.c` | `compile_complete` |

PC7 在旧 `.ioc` 中曾作为普通输出出现，但原理图只把它引到扩展连接器，没有板上功能，因此不虚构为 BSP 资源。

## UART 与 DMA

精确器件提供 5 个 USART、5 个 UART 和 1 个 LPUART。当前 PCB 只绑定以下四路：

| 逻辑用途 | 外设 | TX / RX | 接收方式 | IRQ | 状态 |
| --- | --- | --- | --- | --- | --- |
| W800 AT | USART1 | PA9 / PA10，AF7 | ReceiveToIdle，GPDMA1 Channel 0，`USART1_RX` request | UART 7，DMA 7 | `compile_complete` |
| RS-485 1 | USART2 | PA2 / PA3，AF7 | ReceiveToIdle IT | 10 | `compile_complete` |
| RS-485 2 | UART4 | PA0 / PA1，AF8 | ReceiveToIdle IT | 10 | `compile_complete` |
| 调试串口 | USART3 | PC10 / PC11，AF7 | ReceiveToIdle IT | 10 | `compile_complete` |

两路 RS-485 使用 MAX13487 自动方向收发器，不存在软件 DE GPIO。STM32H5 的 GPDMA 通道与 request 是两个独立配置项；USART1_RX 并不天然固定在 Channel 0，当前通道是本板的软件分配。

## SPI、Flash 与 LCD

| 逻辑用途 | 外设与引脚 | 物理参数 | DMA | 状态 |
| --- | --- | --- | --- | --- |
| GD25LQ128 SPI NOR | SPI1：PA5 SCK、PA6 MISO、PA7 MOSI、PA4 CS，AF5 | Mode 0，目标不超过 16 MHz；求解结果 15.625 MHz | 无 | `compile_complete` |
| ST7796 LCD | SPI2：PB10 SCK、PC2 MISO、PC1 MOSI，AF5；PD11 CS、PD12 DC、PB4 RESET | RGB565，目标不超过 62.5 MHz | GPDMA1 Channel 7，`SPI2_TX` request | `compile_complete` |

SPI 频率由 `mcu_spi.c` 根据实际内核时钟和目标 Hz 求解分频，不在应用里手算 Prescaler。Flash 驱动通过 `osal_mutex` 串行化 UI 资源读取和 OTA 写入；当前工程选择 ThreadX 后端，裸机工程替换后端即可复用设备驱动。

## I2C 与触摸

| 逻辑用途 | 外设与引脚 | 电气/时序约束 | 状态 |
| --- | --- | --- | --- |
| FT6336 触摸 | I2C1：PB8 SCL、PB9 SDA，AF4；PB14 INT、PB15 RESET | 板上外部上拉，GPIO 不再打开内部上拉；TIMINGR 根据实际时钟和目标速率求解 | `compile_complete` |

## PWM

| 逻辑用途 | 定时器资源 | 配置接口 | 状态 |
| --- | --- | --- | --- |
| LCD 背光 | TIM2 Channel 4，PB11 AF1 | 输入频率 Hz 与占空比千分数；自动求解 PSC/ARR 并返回实际值 | `compile_complete` |

默认背光频率为 20 kHz。应用不接触 PSC、ARR、CCR，也不依赖 CubeMX 定时器初始化。

## FDCAN、USB 与 RTC

| 逻辑用途 | 资源 | 初始化策略 | 状态 |
| --- | --- | --- | --- |
| FDCAN1 | PB7 TX / PE0 RX，AF9 | BSP API 已具备，默认不自动启动总线 | `compile_complete` |
| FDCAN2 | PB6 TX / PB12 RX，AF9 | BSP API 已具备，默认不自动启动总线 | `compile_complete` |
| USB FS Device | PA11 DM / PA12 DP，HSI48，静态 PMA 布局 | `board_usb.c` 独占 PCD 和 IRQ，USBX 只经适配层访问 | `compile_complete` |
| RTC | LSE 32.768 kHz | 保留备份域；检测到冲突时拒绝破坏性改源；默认不改当前时间 | `compile_complete` |

FDCAN 与 RTC 暂不在 `bsp_init()` 中自动启用，避免仅为迁移 BSP 就产生总线报文或修改后备域。

## CubeMX 外设替换结果

工程已删除编译与文件系统中的 `gpio`、`gpdma`、`spi`、`i2c`、`usb`、`dcache`、`icache`、`memorymap` 和 CubeMX TIM timebase 源文件。`main.c` 不再调用任何 `MX_*` 外设初始化；ThreadX、USBX、NetX Duo 和 FileX 的应用入口已改为明确的 `app_*_init` 命名。

Keil 分组固定为：

- `BSP/Common`：逻辑 API、状态、时间、缓存、临界区；
- `BSP/MCU`：STM32H5 寄存器/HAL 与物理参数求解；
- `BSP/Board`：当前原理图的引脚、时钟源和资源绑定；
- `BSP/Device`：GD25LQ128、ST7796、FT6336；
- `OSAL/ThreadX`：当前 RTOS 后端，可在裸机目标中替换。

## 编译期检查

`board_resources.h` 对精确 MCU、GPIO 重复占用、DMA 通道冲突、request 存在性和 NVIC 优先级范围执行编译期检查。它不能代替数据手册的封装 AF 表，也不能证明实际 PCB 焊接与原理图完全一致。

## 后续硬件验收

- 测量系统、SPI、I2C、PWM 与四路 UART 的实际频率和空闲电平；
- 验证 W800 DMA 在 IDLE、半包、满缓冲、ORE 和连续数据下不丢失或重复；
- 验证两路 MAX13487 自动方向和最后停止位发送；
- 验证 LCD DMA、触摸中断、Flash 擦写与 D-Cache 一致性；
- 验证 USB 枚举/重连、双 FDCAN 收发、RTC 断电保持和错误恢复；
- 验证状态灯、W800 RESET/BOOT/WAKE 与触摸 RESET 的实际极性。

## 官方资料

- [STM32H562/H563 数据手册](https://www.st.com/resource/en/datasheet/stm32h563ri.pdf)
- [RM0481 参考手册](https://www.st.com/resource/en/reference_manual/rm0481-stm32h533-stm32h563-stm32h573-and-stm32h562-armbased-32bit-mcus-stmicroelectronics.pdf)
- [AN5593 GPDMA 应用说明](https://www.st.com/resource/en/application_note/an5593-how-to-use-the-gpdma-for-stm32u5-series-microcontrollers-stmicroelectronics.pdf)
