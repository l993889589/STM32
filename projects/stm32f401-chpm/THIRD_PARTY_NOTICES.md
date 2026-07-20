# Third-party notices and provenance

- STM32CubeF4 HAL、CMSIS device files：STMicroelectronics，许可文本位于 `Drivers/STM32F4xx_HAL_Driver/LICENSE.*` 与 `Drivers/CMSIS/Device/ST/STM32F4xx/LICENSE.*`。
- CMSIS：Arm，许可文本位于 `Drivers/CMSIS/LICENSE.txt`。
- Eclipse ThreadX 与 USBX：许可文本位于 `Middlewares/ST/threadx/LICENSE.txt`、`Middlewares/ST/usbx/LICENSE.txt`；STM32 限定硬件说明同目录 `LICENSED-HARDWARE.txt`。
- `ld_modbus` 0.2.0：Git submodule `https://github.com/l993889589/ld_modbus.git`，锁定提交 `49657fd13636ee4dec50947145066061fb3b960c`，Apache-2.0，完整许可位于 `third_party/ld_modbus/LICENSE`。
- LDC 2.0.2：Git submodule `https://github.com/l993889589/ldc.git`，锁定提交 `d795674b47a760f02e8f253c1530b41d2d83c22f`；板级锁、静默策略与 ThreadX 通知均在 `user/ldc/dwin_ldc_channel.c`，集成限制见 `user/ldc/PROVENANCE.md`。
- 旧 Armfly 风格外设代码仅作为迁移来源；目标工程已把关键 SPI Flash、PWM、RTOS 队列和通信所有权改写为本项目实现。

两个独立库以 mode-160000 gitlink 保持上游源码不变，CHPM 只实现板级适配。
