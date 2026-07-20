# CHPM 扁平式 BSP

本目录面向一块 STM32F401CC 板和一个 ThreadX 运行时。每个外设模块直接持有自己的
GPIO、时钟、底层句柄、DMA 和中断，不再使用 `board_config`、通用 GPIO/TIM
转发层或多板 backend/OSAL。

## 模块与资源

- `bsp.c/.h`：按依赖顺序组合各模块；不包含产品业务。
- `bsp_clock`：25 MHz HSE、84 MHz 系统时钟及运行时校验。
- `bsp_dwt`：DWT 周期计数和微秒延时。
- `bsp_led`：PC13 低电平有效状态灯。
- `bsp_control`：PB14/PB15 上电安全输出，默认保持低。
- `bsp_uart`：USART1 Modbus、USART2 DWIN；各自持有 RX circular DMA 与 IRQ。
- `bsp_spi`：PA5/PA6/PA7 SPI1 mode 0，有界轮询传输；不暴露共享全局缓冲区。
- `bsp_i2c`：PB6/PB7 开漏软件 I²C，向设备层提供完整事务。
- `bsp_pwm`：PA8/TIM1_CH1 风扇 PWM，使用 Hz 和万分比求解定时器参数。
- `bsp_usb`：PA11/PA12 USB FS、PCD 句柄、FIFO 和 OTG_FS IRQ。
- `bsp_timebase`：TIM11 HAL 1 ms 时基及其 IRQ。
- `bsp_reset`、`bsp_stop`、`bsp_health`：复位原因、致命停机与健康策略。

`bsp_sensor` 把板级 I2C/1-Wire 接到 `third_party/sensors` 的纯 C
AHT20、DS18B20 驱动；`bsp_onewire` 独占 PB0 并使用 `bsp_dwt`，不占用
TIM4。`drv_w25qxx` 仍是板载 Flash 器件模块，并独占 PA4 片选。

## 约束

- app、services、protocol 不得引用 STM32 外设实例、GPIO、HAL 句柄、DMA 或 IRQ。
- 一个 MCU 资源只能有一个初始化所有者。
- PA6 固定为 SPI1 MISO，不得恢复旧 TIM3 PWM。
- 中断只交付字节和事件，不执行 Modbus PDU、Flash 写入或阻塞等待。
- 工程继续使用 ThreadX/USBX；本目录不提供第二套 RTOS 抽象。
