# 问题记录

## ISSUE-004：新增触摸 BSP 后开机 HardFault

- 发现时间：2026-06-26。
- 现象：加入 I2C1 + FT6336U 触摸 BSP 后，用户烧录运行出现 HardFault。
- 直接风险点：
  1. `bsp_init()` 同时在 `main.c` 和 `app_board_io_init()` 中被调用，导致 BSP 重复初始化。
  2. 触摸初始化在开机阶段立即访问 I2C 总线；如果 I2C timing、引脚、触摸上电时序或硬件连接有问题，会在系统启动关键路径上放大故障。
- 处理：
  1. 将 `bsp_init()` 改为幂等函数，重复调用直接返回。
  2. `bsp_touch_init()` 只做触摸复位和状态清零，不再开机读 I2C。
  3. `touch status/read` shell 命令触发时再懒探测 FT6336U 地址 `0x38`。
- 当前状态：已修改并通过 Keil 编译，`0 Error(s), 0 Warning(s)`。需要实板复测确认 HardFault 是否消失。
## ISSUE-001：执行 `modbus status` 后单片机疑似死机

- 发现时间：2026-06-24。
- 现象：W800 正在 TCP socket 连接时，通过 shell 输入 `modbus status`，随后 USB 日志停止。
- 初步结论：高度怀疑是 USBX app 线程栈不足或 shell/status 打印路径过重，不是 Modbus 协议栈本身直接导致。
- 证据：
  - shell 命令在 USB 输入上下文中执行。
  - status 命令可能构造较大的状态结构并格式化多个 64 位计数。
  - `%llu`、`vsnprintf` 和较大的局部变量会增加小栈线程风险。
- 建议：
  1. 增大 USBX app thread 栈。
  2. `modbus status` 只读取 RS485/Modbus 必需状态，避免构造完整板级状态。
  3. shell 输入和命令执行解耦，后续放到独立 shell task。
  4. 增加 fault 现场记录和 ThreadX stack checking。
- 当前状态：已定位方向，是否彻底修复需要后续硬件复测。

## ISSUE-002：LDC 上层存在任务/依赖膨胀风险

- 发现时间：2026-06-24。
- 现象：每增加一个外设，容易复制出 endpoint、task、parser/session、状态机、日志输出链路。
- 结论：
  - LDC core 不应承担业务路由。
  - 可选 Message Bus 放在 LDC 上层，只处理轻量业务事件。
  - 默认关闭 Message Bus，保留当前直连路径。
- 当前状态：已实现可选 Message Bus foundation，并由 `APP_ENABLE_MSG_BUS` 控制。

## ISSUE-003：文档分散且内容冲突

- 发现时间：2026-06-26。
- 现象：根目录、rules、源码目录、shared、tests 下都有项目文档；部分文档编码损坏，内容和当前实现冲突。
- 处理：
  - 项目自写 Markdown 统一迁移到 `docs/`。
  - 删除旧乱码草稿和重复设计文档。
  - 以 `docs/architecture.md`、`docs/message-bus.md` 和 `docs/communication-porting.md` 作为当前权威说明。
- 当前状态：本次清理中处理。
