# STM32H563 通信与应用分层

## 数据路径

所有流式输入遵循同一条路径：

```text
HAL UART/USB callback
        -> BSP transport callback
        -> ldc_endpoint_write()
        -> LDC framing and packet queue
        -> ThreadX event flag
        -> protocol consumer
        -> device/application state machine
```

各层职责固定如下：

- `user/bsp`：引脚、UART/SPI 句柄绑定和 HAL 回调转发，不解析协议。
- `shared/ldc`：纯 C 字节流缓存、分帧、packet FIFO 和统计；无 HAL、ThreadX 或业务代码。
- `user/ldc/ldc_endpoint_threadx.*`：LDC 与 ThreadX Event Flags 的唯一适配层。
- `user/at/core`：AT 行解析、URC、同步会话和命令计划。
- `user/at/modules`：具体模组命令及响应解析。
- `user/app/app_*`：服务初始化、协议消费和业务状态机。

## LDC 输入与分帧

LDC 同时支持：

- `ldc_putc()`：RXNE 等单字节输入。
- `ldc_write()`：DMA、UART ReceiveToIdle、USB 等块输入。
- 分隔符提交：AT 文本使用 `\n`。
- 最大长度提交：固定长度协议或安全上限。
- 静默超时提交：RS485 Modbus RTU。
- `ldc_flush()`：USB transfer 或硬件定时器已经给出明确边界。

`app_ldc_config.c` 只描述每个逻辑端口的 framing policy，不拥有缓冲区、线程或 UART。

## ThreadX endpoint

`ldc_endpoint_threadx` 把 RTOS 内容留在核心库外：

- ISR 写入后发送 RX activity event。
- 分隔符/定长 packet 产生后立即唤醒任务。
- 只有存在未完成帧时才等待静默超时；完全空闲时永久阻塞。
- 使用真实经过时间调用 `ldc_tick()`，不按任务循环次数假设时间。
- LDC callback 在核心临界区退出后才执行。

RS485、W800、NearLink 和 USB 都使用相同 endpoint 接入方式。

## 应用服务

- `app_board_io.c`：板级初始化和状态汇总，不包含协议实现。
- `app_usb_service.c`：USB CDC、Vendor Bulk、USB OTA 数据通路。
- `app_rs485.c`：RS485 endpoint、Modbus RTU slave 和统计。
- `app_w800.c`：W800 endpoint、AT session、Wi-Fi/MQTT 状态机。
- `app_nearlink.c`：NearLink endpoint、角色请求和数据服务。

## AT 模组策略

AT 层采用表驱动与状态机的组合：

- 线性的必选/可选配置命令使用 `at_command_plan`。
- 探测、硬件复位、保存后重启、联网、角色切换和退避使用显式状态机。
- W800 和 NearLink 不共享伪造的“万能网络接口”；只共享 session、command plan、日志和超时机制。

新增模组时：

1. 在 `user/at/modules` 创建驱动并实现设备动作。
2. 用 `at_command_plan` 表达线性配置步骤。
3. 在独立 `app_xxx.c` 中创建 endpoint、session 和设备状态机。
4. 在 `app_board_io_init()` 中注册服务。
5. 为命令计划和响应解析增加主机测试。

## 测试和验收

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

主机测试覆盖：

- 字节输入与任意块切分结果一致。
- 分隔符、定长、静默超时和手动 flush。
- 环形缓冲回绕、小缓冲重试。
- PROTECT/OVERWRITE 队列满行为。
- 回调不在核心临界区执行。
- 无效初始化参数。
- AT 必选/可选命令计划。

目标板验收还应检查 Shell 中每条链路的 `overflow/drop/peak`，并执行长时间高负载测试。
