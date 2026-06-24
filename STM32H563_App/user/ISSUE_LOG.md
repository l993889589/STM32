# 问题台账

这个文件用来记录实际调试中定位到的问题。每个问题尽量包含：现象、触发条件、初步结论、证据、建议处理和当前状态。

## ISSUE-001：执行 `modbus status` 后单片机死机

- 发现时间：2026-06-24
- 现象：W800 正在连接 TCP socket 时，通过 shell 发送 `modbus status`，随后 USB 日志停止，表现为单片机死机或进入 fault。
- 触发条件：
  - CDC shell 收到 `modbus status`。
  - shell 命令在 USBX Device App Main Thread 中直接执行。
  - 该线程栈大小当前为 1024 bytes。
- 初步结论：高度疑似 USBX 线程栈溢出，不是 Modbus 协议栈本身导致。
- 证据：
  - `STM32H563_App/USBX/App/app_usbx_device.h` 中 `UX_DEVICE_APP_THREAD_STACK_SIZE` 为 1024。
  - `STM32H563_App/USBX/App/app_usbx_device.c` 中 USBX 线程局部变量包含 `rx_buffer[256]`。
  - `shared/shell/shell.c` 的 `shell_execute()` 中有 `command_line[128]`、`argv[SHELL_MAX_ARGUMENTS]` 等局部变量。
  - `shared/shell/shell.c` 的 `shell_printf()` 中有 `char buffer[192]`，并调用 `vsnprintf()`。
  - `STM32H563_App/user/app/app_shell.c` 的 `modbus status` 使用局部 `app_board_status_t status`。
  - `app_board_status_t` 内包含 3 份 `ldc_stats_t`，而 `ldc_stats_t` 内部是多个 `uint64_t` 计数器。
  - `app_shell_print_ldc()` 使用多个 `%llu` 输出 64 位 LDC 统计，进一步增加格式化栈压力。
- 建议修法：
  1. 立刻止血：把 USBX app thread 栈从 1024 提高到 4096。
  2. 立刻止血：`modbus status` 只读取 RS485/Modbus 相关统计，不再构造完整 `app_board_status_t`。
  3. 后续优化：shell 输入只在 USBX 线程入队，命令执行放到独立 shell task。
  4. 后续优化：状态输出避免在小栈线程里集中使用 `%llu`，必要时拆成轻量格式化函数。
  5. 后续优化：开启 ThreadX stack checking 或增加 fault 现场记录，便于确认栈溢出和 fault 类型。
- 当前状态：已定位，尚未修复。

## ISSUE-002：LDC 上层存在任务/依赖膨胀风险

- 发现时间：2026-06-24
- 现象/背景：
  - 当前 LDC core 本身是轻量的帧化接收内核，负责字节/块输入、ring buffer、packet descriptor FIFO、分帧和事件通知。
  - 但是上层每增加一个外设，容易继续复制出一套 `endpoint + task + parser/session + 状态机 + 直接日志输出`。
  - 如果未来继续按点对点方式扩展，容易出现“外设多了以后队列爆炸、任务爆炸、依赖关系乱、debug 困难”的架构风险。
- 初步结论：
  - 不建议改 LDC。LDC 应保持为 RTOS-free 的轻量链路接收内核。
  - 建议在 LDC 之上新增系统 Message Bus / Event Bus 层，用中央调度和 handler 表把上层依赖收束起来。
- 证据：
  - `shared/ldc/ldc_core.*` 只包含 ring、packet queue、callback、统计和锁注入，没有业务路由职责。
  - `user/ldc/ldc_endpoint_threadx.*` 已经把 LDC 事件转换为 ThreadX event flags。
  - `app_rs485.c`、`app_w800.c`、`app_nearlink.c`、`app_usb_service.c` 各自持有 endpoint、缓冲区、协议消费和状态机。
  - 多个服务模块直接调用 `app_usb_cdc_write()` 输出日志，日志链路和业务链路耦合。
- 建议方向：
  1. 保留 LDC 作为每条物理链路的 ingress/framing 层。
  2. 新增 `app_msg_bus`，使用固定大小消息、静态队列、静态 handler 表，不使用动态内存。
  3. UART/USB/LDC 只发布 `LINK_FRAME`、`LINK_OVERFLOW`、`LINK_DROP`、`CONTROL` 等消息。
  4. Modbus、AT、Shell、Log、OTA 等都改成 handler/subscriber，新增外设主要新增 handler 和配置表。
  5. 建立规则：bus handler 不能长时间阻塞；耗时动作拆成异步状态机或 worker job。
  6. 日志统一走 `LOG` 消息，由低优先级 log handler 输出，避免各模块直接写 CDC。
- 当前状态：已识别为架构风险，尚未实施 Message Bus。
