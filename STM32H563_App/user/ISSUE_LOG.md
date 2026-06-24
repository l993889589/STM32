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
