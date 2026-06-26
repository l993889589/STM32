# 问题记录

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
