# CHPM 板级功能清单

## 处理器与时钟

- MCU：STM32F401CCU6，UFQFPN48。
- HSE：25 MHz。
- PLL：M=25、N=336、P=4、Q=7。
- SYSCLK/HCLK：84 MHz；PCLK1：42 MHz（APB1 定时器 84 MHz）；PCLK2：84 MHz；USB 时钟：48 MHz。
- Keil 内存：IROM 0x08000000–0x0803FFFF（256 KiB），IRAM 0x20000000–0x2000FFFF（64 KiB）。

## 引脚与所有者

| 引脚 | 功能 | 软件所有者 | 说明 |
|---|---|---|---|
| PA2/PA3 | USART2 TX/RX | `bsp_uart` / DWIN | 115200，RX 为 DMA1 Stream5 Channel4 |
| PA9/PA10 | USART1 TX/RX | `bsp_uart` / Modbus RTU | 115200，RX 为 DMA2 Stream2 Channel4；板上没有 RS485 收发器 |
| PA11/PA12 | USB FS D-/D+ | USBX + STM32 DCD | CDC ACM，VBUS sensing 关闭 |
| PA4 | W25Q64 CS | `drv_w25qxx` | 高电平空闲 |
| PA5/PA6/PA7 | SPI1 SCK/MISO/MOSI | `bsp_spi` / W25Q64 | DMA2 Stream0 RX、Stream3 TX；PA6 不得再作为 TIM3 PWM |
| PA8 | TIM1 CH1 | `bsp_pwm` | 风扇 PWM，25 kHz，占空比单位 0.01% |
| PB6/PB7 | 软件 I2C SCL/SDA | `bsp_i2c_soft` / AHT20 | 原理图有 4.7 kΩ 上拉 |
| PB0 | 1-Wire | `drv_ds18b20` | 当前活动的 DS18B20 通道 |
| PB1 | 第二路 1-Wire 接口 | 保留 | 旧 Modbus 曾把它当“继电器”，与原理图冲突，因此默认禁止远程脉冲 |
| PC13 | 状态 LED | monitor 线程 | LED2 低电平点亮 |
| PB14/PB15 | 旧控制输出 | `bsp_gpio` | 原理图未给出业务名称，保持上电输出低，必须实机确认用途 |
| PA13/PA14 | SWDIO/SWCLK | 调试接口 | 本任务未调用任何探针或下载动作 |

TIM4 由 `bsp_timer` 独占，配置为 1 MHz 计数供 1-Wire 微秒延时。TIM11 由 HAL timebase 使用；ThreadX 使用 1 kHz 系统节拍。

## 外部器件与业务

- W25Q64JV：8 MiB，4 KiB 扇区，256 B 页；参数只使用前 12 KiB，详见 `STORAGE_LAYOUT.md`。
- AHT20：温湿度，每 10 s 采样。
- DS18B20：每 10 s 发起转换，等待 750 ms 后读 9 字节 scratchpad 并校验 CRC8。
- DWIN 屏：USART2 自定义 `5A A5` 帧，保留电脑指标、时间、事件表、报警颜色和页面切换行为。
- USB：USBX CDC ACM，保留 VID:PID 0483:5740、产品字符串和端点号。
- Modbus：USART1 RTU 从站；外部 RS485 电气层、DE/RE 方向控制和终端电阻不在板上，必须结合外接模块实测。
