# 双工程打包约束

用户要求裸机和 ThreadX 工程能够分别直接复制，因此本工作区采用两份完整 BSP，而不是顶层 shared 源码。

约束如下：

1. `luoji` 和 `THREADX` 均包含自己的 CMSIS、HAL、BSP、LDC、OSAL、transport 和 Keil 工程。
2. 任一 Keil 工程不得通过绝对路径或 `../..` 引用另一个工程或根目录源码。
3. 为降低熟悉成本，每份工程的 BSP 实体文件统一平铺在 `user/bsp`；公共 API 使用 `bsp_*`，板级绑定使用 `board_*`，STM32H5 私有实现使用 `*_stm32h5` 后缀。
4. ThreadX 类型只允许进入 ThreadX 工程的 OSAL、Core integration 和 middleware。
5. 两份 BSP 的公共 API 和公共实现应保持一致；运行模式差异只位于 `target_config.h`、main、OSAL 和 ThreadX middleware。
6. 修改公共 BSP、LDC、transport 或 device driver 后，必须同步另一份并执行两个工程的 clean build。
7. 不允许为了同步方便重新引入顶层 shared 编译依赖。

当前任务例外（2026-07-10）：用户明确要求板载外设长任务只写裸机，因此新增 I2C/LCD/Touch/FDCAN/RTC/USB/W800/Flash 完整接口只进入 `luoji`，`THREADX` 冻结在上一 clean-build 基线。接口仍按 RTOS 可复用边界设计，但“同步另一份并双目标构建”延后到用户单独授权 RTOS 移植时执行；这不是重新引入 shared 的许可。

这种打包方式牺牲了单一源码来源，但换取每个工程文件夹可独立复制。同步责任通过双目标编译和文件比较承担。
