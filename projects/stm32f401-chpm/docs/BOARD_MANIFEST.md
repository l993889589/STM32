# CHPM STM32F401CC 板级资源清单

## 时钟

- HSE：25 MHz。
- PLL：M=25、N=336、P=4、Q=7。
- SYSCLK/HCLK：84 MHz。
- PCLK1：42 MHz；APB1 定时器时钟：84 MHz。
- PCLK2：84 MHz；APB2 定时器时钟：84 MHz。
- USB：48 MHz。

## 引脚与唯一所有者

| 引脚 | 功能 | 唯一软件所有者 | 说明 |
|---|---|---|---|
| PA2/PA3 | USART2 TX/RX | `bsp_uart` / DWIN | 115200；RX DMA1 Stream5 Channel4 |
| PA9/PA10 | USART1 TX/RX | `bsp_uart` / Modbus | 115200；RX DMA2 Stream2 Channel4 |
| PA11/PA12 | USB FS D-/D+ | `bsp_usb` | USBX CDC ACM；关闭 VBUS sensing |
| PA4 | W25Q64 CS | `drv_w25qxx` | 空闲为高 |
| PA5/PA6/PA7 | SPI1 SCK/MISO/MOSI | `bsp_spi` | mode 0；PA6 不得复用为 PWM |
| PA8 | TIM1 CH1 | `bsp_pwm` | 风扇 25 kHz；占空比单位 0.01% |
| PB6/PB7 | 软件 I²C SCL/SDA | `bsp_i2c` | 开漏；板上应有上拉 |
| PB0 | 1-Wire | `bsp_onewire` | 为纯 C `sensors/ds18b20` 提供 DWT 微秒时序，不占用通用定时器 |
| PB1 | 第二路 1-Wire 预留 | 未启用 | 禁止按旧代码当继电器驱动 |
| PB14/PB15 | 未命名控制输出 | `bsp_control` | 上电保持低，实机确认用途前不扩展业务 |
| PC13 | 状态 LED | `bsp_led` | 低电平点亮 |
| PA13/PA14 | SWDIO/SWCLK | 调试接口 | 本次只编译，未连接或操作探针 |

TIM11 只属于 `bsp_timebase`，用于 HAL 1 ms tick。ThreadX 由 Cortex-M4 AC6 端口
提供 1 kHz 系统节拍。TIM2/TIM3/TIM4/TIM5 不再被遗留通用定时器模块占用。

## 外部器件

- W25Q64JV：8 MiB、4 KiB 扇区、256 B 页。
- AHT20：PB6/PB7 软件 I²C。
- DS18B20：PB0 单总线。
- DWIN：USART2。
- Modbus RTU：USART1 TTL 侧；外接 RS485 收发器、电阻和方向控制需实机确认。
- USB：USBX CDC ACM，VID:PID 0483:5740。
