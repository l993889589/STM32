# W800 HTTP Range 图片更新五阶段执行总控

本文是 W800 图片更新稳定化工作的主线记录。五个阶段已于 2026-07-10 全部完成，详细实机证据见 `docs/w800_http_range_acceptance_report.md`。

## 总体状态

| 阶段 | 状态 | 完成结果 |
| --- | --- | --- |
| 1. HTTP Range 与 A/B 区闭环 | 已完成 | 4 KiB Range、inactive slot 写入、整包 CRC 后 commit |
| 2. 构建和烧录链路固化 | 已完成 | Keil build + pyOCD program/verify + reset 后 build ID 验证 |
| 3. AT Core 工业化整理 | 已完成 | 长度驱动 raw receive、模组分隔策略、诊断计数和主机测试 |
| 4. Desktop Debug Assistant 配套 | 已完成 | HTTP/MQTT 服务、资源发布、故障注入和 Electron 稳定性修复 |
| 5. 故障与连续更新压力测试 | 已完成 | HTTP/MQTT 中断、MCU 复位、10 次更新、LVGL 和防降级均通过 |

## 固化边界

- 图片大文件数据面只走 HTTP Range。
- MQTT 只做命令、状态、进度和错误上报，不承载图片数据。
- MQTT 与 HTTP 可以同时存在；MQTT 暂停不阻断正在进行的 HTTP 数据面。
- W800 socket ID 是模组内部网络通道，不代表 UART AT 会话可并发。
- 所有 AT 命令必须通过同一个 `at_session_t` 串行执行。
- W800 LDC 不使用 `\n` delimiter 对混合 AT 文本和二进制流断帧。
- 网络 payload 先解析长度，再进入 exact-length raw window。
- inactive slot 未完整写入并通过整包 CRC 前，不允许切 active slot。
- 下载失败、网络中断或 MCU 复位只能保留旧图，不允许提交半包。
- 版本必须单调递增，旧版本默认拒绝。

## 阶段 1：HTTP Range 与 A/B 区闭环

状态：已完成。

- Desktop Debug Assistant 提供 `/ui/manifest.json` 和 `/ui/ui_assets.bin`。
- manifest 包含 `version`、`size`、`crc32`、`path`、`chunkSize`。
- HTTP 支持 `206 Partial Content`、`Content-Range`、`Accept-Ranges` 和 Range CRC。
- 固件通过 MQTT 命令触发 `ui_http_manifest_update`。
- 固件只写 inactive slot，整包 CRC 通过后提交。
- 实机故障测试证明连接中断和 MCU 复位都不会切换 active slot。

## 阶段 2：构建和烧录链路固化

状态：已完成。

日常入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build -WaitMqttSeconds 120
```

- 应用区：`0x08020000-0x081FFFFF`。
- Bootloader 保护区：`0x08000000-0x0801FFFF`。
- 脚本打印实际 HEX/AXF 路径、时间和大小。
- pyOCD 擦除应用区并执行 program/verify。
- MQTT 日志按 `1883` 端口拥有进程定位，避免读取旧 release 日志。
- 上线验证只接受 reset 完成后的新状态，避免误用擦除期间的旧心跳。
- 最终构建：`0 Error(s), 0 Warning(s)`。
- 最终固件：`fwBuildId="Jul 10 2026 13:29:20"`。
- 烧录脚本会等待 UV4 结束、校验新 artifact 时间，并自动匹配 AXF build ID，已消除“构建未结束就烧录旧 HEX”的竞态。

## 阶段 3：AT Core 工业化整理

状态：已完成本轮通用边界和 W800 实现。

- `at_core` 提供 bounded exact-length raw receive window。
- `at_session` 统一命令串行化、超时和 binary diagnostics。
- 新增显式 raw separator policy，通用 Core 默认不吞额外 CR/LF。
- W800 driver 显式使用 `AT_RAW_SEPARATOR_EMPTY_LINE`。
- 严格解析 payload 长度，拒绝空字段、非法字符和尾随垃圾。
- MQTT 状态提供 binary attempts/success/errors 诊断。
- 6 项主机侧 framing/recovery 测试通过。
- 最终 10 轮实机中 binary `19088/19088` 成功，错误为 0。

边界说明：EC20 的 socket receive 仍需按其 `QIRD` 响应格式单独实现；这不阻塞本轮 W800 验收，也不能直接复用 W800 的额外空行策略。

## 阶段 4：Desktop Debug Assistant 配套

状态：已完成并重新打包 release。

- Electron `send()`、`second-instance` 和 delayed socket write 均增加 destroyed/closed 防护。
- HTTP 服务支持 4 KiB Range 和包/块 CRC header。
- MQTT 与 HTTP 监听由同一 release 管理。
- 物理网卡地址优先于 Hyper-V/WSL 虚拟网卡。
- 增加仅 loopback 可用的 HTTP 故障注入和 MQTT Broker 启停接口。
- Broker stop 会主动销毁现有 TCP client，测试不会因旧连接挂起。

## 阶段 5：压力测试

状态：已完成。

- HTTP Range 中断：旧版本保持不变，恢复后成功。
- MQTT 中断：HTTP 继续到最后一个 Range，Broker 恢复后补报成功。
- 更新中 MCU 复位：不提交半包，旧图可读，重试成功。
- 连续真实资源包更新：10/10 通过，共 `15605760` 字节。
- 单次耗时：456 到 477 秒，平均 466.1 秒。
- A/B slot 严格交替，最终版本 `2026071024`、slot 0。
- LVGL 运行 66 秒，跨 3 个以上翻页周期，decoder `readFail=0`。
- 旧版本 `2026071023` 被拒绝，active version `2026071024` 不变。

## 旁支队列

以下项目不属于本次 W800 HTTP Range 主线完成条件：

- Modbus 实际设备 timeout 的现场定位。
- EC20 `QIRD`/socket receive 实现与测试。
- USB OTA 上传后的自动恢复流程复盘。
- 产品级掉电寿命、弱信号、EMC/ESD 和 Flash endurance 资格测试。

## 不再采用的方向

- 不使用 MQTT base64 chunk 作为图片数据 fallback。
- 不把 W800 socket ID 当成 UART 并发能力。
- 不使用 LDC `\n` delimiter 处理混合 AT 文本和二进制 payload。
- 不在 CRC 通过前切换 active slot。
