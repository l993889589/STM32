# 全板自动自检

## 目标

本工程的自检不是简单的“初始化返回成功”，而是为每项板载资源输出稳定、可机读的结果。结果明确分为：

- `passed`：已获得运行时或物理链路证据。
- `failed`：资源存在且测试已执行，但结果错误。
- `not_connected`：器件或接口存在，但当前没有外部线缆、从站或对端响应。
- `not_installed`：当前板型没有安装对应器件。
- `not_run` / `testing`：尚未执行或正在执行。

## 覆盖资源

共 15 项：状态 LED、LCD、背光 PWM、触摸 FT6336U、GD25LQ128、W800、W800 UART DMA、调试 UART IRQ、RS485-1、RS485-2、FDCAN1、FDCAN2、RTC、USB、黑匣子。

双 FDCAN 使用板上已连接的物理交叉链路持续验证双向收发、延迟和错误计数。RS485 没有现场从站响应时报告 `not_connected`，不会误报为硬件失败。

## 运行方式

ThreadX 的 `Whole Board Self Test` 线程在上电稳定后自动执行一次；也可以通过 Shell 重新触发：

```text
self_test status
self_test run
```

LCD 增加了独立的 `BOARD AUTO SELF TEST` 页面，原有图片页面和双 CAN 页面均保留。页面切换命令：

```text
ui page self_test
ui next
```

每轮总结会写入 RTC+SPI NOR 黑匣子，详细结构化快照由 `app_self_test_get_snapshot()` 提供。

## 2026-07-11 实机证据

- Clean Build：0 errors、0 warnings。
- 首轮及低功耗唤醒后复验：13 passed、0 failed、2 not_connected、0 not_installed。
- 两项 `not_connected` 为 RS485-1 与 RS485-2，原因均为没有现场从站响应。
- LCD、触摸、SPI NOR、W800、USB、双 FDCAN、RTC 和黑匣子均通过。

