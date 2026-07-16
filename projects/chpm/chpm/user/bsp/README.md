# CHPM flat BSP

本目录采用单板、单 MCU、单 ThreadX 运行时的扁平 BSP；不复制多板卡 backend/OSAL 层。

- `board_config.h`：唯一板级常量和资源映射。
- `board.c`：确定性初始化顺序；不承载业务。
- `bsp_system`：HAL、84 MHz 系统时钟、DWT 和错误入口。
- `bsp_gpio`：PC13 与遗留 PB14/PB15。
- `bsp_uart`：USART1/2 + ReceiveToIdle circular DMA；IRQ 回调只交付字节块。
- `bsp_spi`：SPI1 DMA，总线传输有界等待。
- `bsp_timer`：TIM4 1 MHz 微秒时基。
- `bsp_pwm`：TIM1_CH1/PA8 风扇 PWM，运行时时钟求解。
- `bsp_i2c_soft`：PB6/PB7 AHT20 软件 I2C。
- `bsp_health`：外设健康计数和轮询。

所有 MCU 资源必须只有一个初始化所有者。禁止恢复 TIM3/PA6 PWM；PA6 属于 W25Q64 SPI1 MISO。中断中不得执行 Modbus 解析、Flash 写入或阻塞等待。
