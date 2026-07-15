# STM32H563 Stop 低功耗与多源唤醒

## 芯片模式说明

STM32H563 的官方低功耗模式名称是 **Stop mode**，不是 STM32 某些系列使用的 `STOP2`。本工程调用 `HAL_PWR_EnterSTOPMode()`，文档和接口不虚构芯片不存在的模式。

## 支持的唤醒源

- RTC：LSE 32.768 kHz 驱动的一次性秒级唤醒定时器，范围 1～65536 秒。
- 触摸：FT6336U 的 PB14 低有效中断，使用 EXTI14 下降沿唤醒。
- W800：USART1 PA10 接收起始位唤醒。板上的 PC8 `W800_WAKE` 是 MCU 输出，不能反向作为 MCU 唤醒输入。

所有请求都自动武装 RTC 作为安全超时；如果触摸或 W800 先到达，报告会记录真正的先到唤醒原因。

## 有序进入与恢复

1. 记录黑匣子“准备进入”事件并等待队列落盘。
2. 协作暂停 W800 服务，避免在 HAL UART 事务中途切换状态。
3. 临时提高电源线程优先级，保存并关闭背光 PWM。
4. 停止 USB PCD 并屏蔽 USB IRQ，避免主机流量立即唤醒。
5. 武装 RTC、触摸和/或 USART1 起始位唤醒。
6. 保存所有 NVIC enable 位，只留下本次唤醒 IRQ；另外保存并关闭 Cortex-M33 SysTick 中断、清除 pending。
7. 执行 WFI 进入 H563 Stop。
8. 唤醒后立即复用唯一的 `bsp_clock_configure_system()`，恢复 HSE、PLL1、250 MHz 总线、HSI48 和 USB CRS。
9. 恢复 UART DMA、NVIC、SysTick、USB、触摸普通输入和背光 PWM。
10. 持久化唤醒原因、RTC 实测睡眠秒数和恢复错误计数，随后重新运行全板自检。

## Shell 控制

```text
power status
power stop 3 rtc
power stop 30 touch
power stop 30 w800
power stop 30 all
```

进入 Stop 时 USB 会主动断开，唤醒后重新连接，这是受控行为。

## 实机验收

2026-07-11 使用 H7-TOOL、500 kHz SWD、under-reset 烧录：

- RTC 轮次：请求 3 秒，RTC 实测 3 秒，`wake_reason=rtc`，恢复错误 0。
- W800 轮次：RTC/触摸/W800 全部武装，W800 USART1 启动数据在 10 秒 RTC 兜底前唤醒，`wake_reason=w800`，恢复错误 0。
- 触摸电气轮次：请求 30 秒、武装 RTC+touch；MCU 进入 Stop 后由 SWD 在 PB14 pad 注入真实下降沿，RTC 兜底前以 `wake_reason=touch` 唤醒，恢复错误 0，GPIO 模式和上下拉恢复为原值。
- 两轮恢复后的五个时钟快照均为 250000000 Hz。
- 唤醒后全板自检仍为 13 passed、0 failed、2 not_connected。
- 触摸唤醒已覆盖 GPIO pad→EXTI14→Stop→恢复的实机电气路径；“手指触摸是否让 FT6336U 拉低 INT”仍可人工按屏补做传感器动作验收，SWD 注入没有被描述成手指触摸。

最终常规配置中 `APP_POWER_AUTO_RTC_TEST=0`、`APP_POWER_AUTO_W800_TRIGGER_TEST=0`，不会在每次上电时自动休眠或复位 W800。
