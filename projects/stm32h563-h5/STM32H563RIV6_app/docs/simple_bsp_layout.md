# STM32H563RIV6_app 扁平 BSP 说明

本工程采用适合个人开发维护的扁平 BSP，不再区分可见的 `mcu_xxx`、`board_xxx`、器件型号驱动和 OS 适配层。

## 目录规则

Keil 中只有一个 `BSP` 分组，源码统一位于 `user/bsp`：

```text
user/bsp/
├── bsp.c
├── bsp.h
├── bsp_config.h
├── bsp_uart.c/.h
├── bsp_spi.c/.h
├── bsp_i2c.c/.h
├── bsp_fdcan.c/.h
├── bsp_pwm.c/.h
├── bsp_flash.c/.h
├── bsp_lcd.c/.h
├── bsp_touch.c/.h
├── bsp_usb.c/.h
└── 其他 bsp_xxx.c/.h
```

## 使用规则

- 每个 `bsp_xxx.c` 同时负责对应功能的引脚、时钟、DMA、中断、初始化和对外 API。
- `bsp_config.h` 集中保存当前板子的引脚、复用功能、外设实例、DMA 请求和硬件参数。
- 板载 GD25LQ128 对应用功能名 `bsp_flash`，应用不再依赖具体芯片型号。
- ThreadX 直接由应用代码使用，BSP 不引入额外的 OS 适配层，也不直接依赖 ThreadX API。
- 更换板子时优先修改 `bsp_config.h`，硬件机制变化时再修改对应的 `bsp_xxx.c`。
- 图形界面只保留 LVGL；未参与构建的 GUIX Keil 分组和中间件目录已经删除。

## 验证状态

- 工程目标：`STM32H563RIV6_app`
- Keil 全量编译：`0 Error(s), 0 Warning(s)`
- LVGL 编译单元：306；GUIX 编译单元：0
- 状态：`compile_complete`
- 未烧录、未连接目标板、未执行硬件验证。
