# AT Core API 审核清单

本文记录 W800 HTTP Range 图片更新稳定化阶段 3 的 AT Core/API 边界审核结果。目标不是把所有 AT 模组一次性做完，而是把已验证能力、未完成缺口和后续接入规则写清楚，避免再次用行分隔策略处理二进制 payload。

## 当前结论

- W800 的 HTTP Range 下载链路已按二进制安全路径接入：`SKSTT -> SKRCV -> at_session_cmd_read_binary_ex() -> at_client_raw_begin_ex()`。
- W800 的 LDC 接收已经禁用 `'\n'` delimiter，AT Core 自己解析 CR/LF 文本行，并在 raw window 中按长度接收二进制 payload。
- `at_module` 已提供显式 socket ID API，可同时管理 MQTT 控制 socket 和 HTTP 数据 socket。
- socket ID 只是模组内部网络通道，不是 UART 并发通道；所有 AT 命令仍必须通过同一个 `at_session_t` 串行执行。
- 通用 AT Core 默认不消费 header 后额外空行；W800 driver 显式选择 `AT_RAW_SEPARATOR_EMPTY_LINE`，其他模组默认使用 `AT_RAW_SEPARATOR_NONE`。
- 二进制长度字段采用严格十进制解析，缺少数字、尾随字符或超容量都会在进入 raw window 前失败。
- `at_session_binary_diag_t` 记录 attempts、successes、header、capacity、raw-arm 和 timeout 计数，板端通过 MQTT 状态上报。
- EC20 目前只有 socket open/send/close 的基础封装，`ec20_recv_socket()` 仍是 stub，不能声明 EC20 已完成二进制安全 socket receive。

## 文件边界

| 文件 | 当前职责 | 审核状态 |
| --- | --- | --- |
| `STM32H563_App/user/at/core/at_core.c/.h` | AT 字节流解析、URC 派发、命令结果状态、exact-length raw receive window | 已提供显式 separator policy；通用层不再硬编码 W800 空行 |
| `STM32H563_App/user/at/core/at_session.c/.h` | 会话级命令发送、等待、日志捕获、平台时间/睡眠/轮询适配、二进制命令读取 | 已提供 `_ex()` 接口、严格长度校验和 binary diagnostics |
| `STM32H563_App/user/at/modules/at_module.c/.h` | AT 模组统一 facade，转发 driver 表并提供显式 socket ID API | 已补 socket ID 与 UART 串行化边界说明 |
| `STM32H563_App/user/at/modules/at_module_w800.c/.h` | W800 WiFi AT driver，封装 `SKCT/SKSND/SKRCV/SKSTT` 等语义 | W800 当前链路可用，`SKRCV` 已返回纯 payload |
| `STM32H563_App/user/at/modules/at_module_ec20.c/.h` | EC20 蜂窝 AT driver，封装网络注册和 socket 基础操作 | `recv_socket` 未实现，后续接 HTTP/文件下载前必须补齐 |
| `STM32H563_App/user/app/app_w800.c` | W800 应用服务，负责 MQTT 控制、HTTP Range 下载、UI asset A/B 更新状态机 | 已使用 `SKSTT` 判断对应 socket 有数据后才 `SKRCV` |

## 必须保持的接口规则

1. AT Core 只能把 CR/LF 当作文本响应边界，不能让 LDC 或 UART 底层用 `'\n'` 对整条 W800 输入流断帧。
2. 任何返回网络 payload 的 AT 命令都必须先解析长度 header，再调用 raw receive window 读 exact length。
3. raw receive 期间不得把 payload 字节送入行解析器，payload 中的 `0x0D/0x0A` 必须原样保留；header separator 只能由具体模组 driver 显式声明。
4. `at_module_recv_socket_id()` 的返回数据必须只包含网络 payload，不允许混入 `+OK=<len>`、`OK`、`ERROR` 或 URC 文本。
5. 多 socket 只能减少网络连接互相占用，不能绕过单 UART session 的命令串行化。
6. 新增模组 driver 必须在 `.c` 文件头说明 AT 命令格式、payload 边界、超时/重试策略和不支持项。

## W800 审核结果

W800 的接收路径当前满足 HTTP Range 数据面要求：

- `app_w800_socket_recv_when_ready()` 先执行 `AT+SKSTT=<id>`，只有对应 socket 的 `rx_data` 非零时才执行 `SKRCV`。
- `w800_recv_socket()` 调用 `at_session_cmd_read_binary_ex()`，不会按行读取 payload。
- W800 显式传入 `AT_RAW_SEPARATOR_EMPTY_LINE`，只跳过 `+OK=<len><CR><LF><CR><LF>` 中属于协议 header 的空行，再按长度复制 payload。
- payload 自身即使以 `0x0D 0x0A` 开头也会原样保留。
- `app_w800` 的 LDC 配置设置 `delimiter_enabled = false`，避免 `0x0A` 密集图片块造成 packet storm。

2026-07-10 阶段 5 实机验证结果：

- 连续 10 次 HTTP Range 更新全部通过。
- 最终 AT binary `19088/19088` 成功，header/capacity/raw-arm/timeout 错误均为 0。
- HTTP 下载中断后旧 active slot 未被污染。
- MCU 复位后半包未 commit。
- MQTT 中断 433 秒期间 HTTP 继续完成，MQTT 恢复后补报最终状态。

主机侧 `tools/test_at_core_host.ps1` 另覆盖 6 个 framing/recovery 场景，包括 W800 空行、payload 自身 CR/LF、非法长度、超容量、partial timeout 和下一命令恢复，结果全部通过。

## EC20 缺口

`at_module_ec20.c` 当前 `ec20_recv_socket()` 返回 `false` 且 `actual_len=0`。这对当前 W800 图片更新链路没有直接影响，但对“多 AT 模组统一接口”来说是明确未完成项。

后续如需 EC20 走 HTTP/文件下载，应补齐：

- 使用 EC20 固件对应的数据读取命令，例如 `AT+QIRD` 或当前固件支持的等价命令。
- 解析长度 header 后进入 `at_session_cmd_read_binary()` 或等价 exact-length raw receive。
- 明确处理零长度、超时、连接关闭、URC 插入和 partial read。
- 增加最小主机侧解析测试，覆盖 payload 中包含 `0x00/0x0D/0x0A` 的场景。

## 阶段 3 验收状态

阶段 3 判定为“通过”：W800 路径已完成实机压力验证，通用 AT 模组边界已固化，EC20 receive 未完成但已明确隔离。

最终 ThreadX/Keil 构建结果为 `0 Error(s), 0 Warning(s)`；这不等价于所有 AT 模组的 socket receive 都已实现。EC20 后续仍需独立完成 `QIRD` exact-length 适配和对应主机测试。
