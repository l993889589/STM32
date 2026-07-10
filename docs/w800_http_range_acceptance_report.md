# W800 HTTP Range UI 资源更新最终验收报告

验收日期：2026-07-10  
结论：**通过本轮软件与实机稳定性验收**。

本报告覆盖 W800 HTTP Range 下载、GD25LQ128 外部 Flash A/B 区、整包 CRC、LVGL 在线读取、MQTT 控制面隔离、AT 二进制接收及构建烧录链路。

## 验收基线

- 固件构建 ID：`Jul 10 2026 13:29:20`。
- Keil 构建：`0 Error(s), 0 Warning(s)`。
- 程序尺寸：`Code=454972 RO-data=399356 RW-data=2048 ZI-data=384096`。
- 资源包：5 页 RGB565，单包 `1560576` 字节。
- HTTP Range：每块 `4096` 字节，最后一块按剩余长度返回。
- UI 自动翻页周期：`20000 ms`。
- 外部 Flash：GD25LQ128。
- Slot A：`0x00500000-0x009FFFFF`。
- Slot B：`0x00A00000-0x00EFFFFF`。
- 应用固件区：`0x08020000-0x081FFFFF`。
- Bootloader 保护区：`0x08000000-0x0801FFFF`。

## 五阶段结论

| 阶段 | 结果 | 主要证据 |
| --- | --- | --- |
| HTTP Range 与 A/B commit | 通过 | 只写 inactive slot；整包 CRC 通过后切 active slot；失败和复位均保持旧版本 |
| 构建与烧录固化 | 通过 | Keil rebuild、pyOCD program/verify、reset、MQTT 新 build ID 检查形成单入口 |
| AT Core 工业化 | 通过 | raw payload 按长度接收；分隔策略从通用 Core 中剥离；6 项主机测试通过 |
| Desktop Debug Assistant | 通过 | manifest、4 KiB Range、CRC headers、故障注入、Broker 控制、窗口销毁防护均已集成到 release |
| 故障与连续更新压力测试 | 通过 | HTTP 中断、MQTT 中断、更新中复位、10 次连续更新、LVGL 运行、降级保护均通过 |

## 实机压力测试

### HTTP 连接中断与 A/B 保护

- 基线版本：`2026071011`。
- 候选版本：`2026071012`。
- 从偏移 `65536` 开始注入连接关闭，共命中 9 次。
- 设备最终报告 `http.error="range retry"`。
- 失败后 active version 仍为 `2026071011`，未提交半包。
- 清除故障后，同一候选版本在 465 秒内恢复成功。
- 最终 `asset.version=2026071012`，错误为空。

证据目录：`tools/logs/w800_http_range_fault_20260710_110615`。

### MQTT 中断时 HTTP 数据面继续工作

- 基线版本：`2026071012`。
- 候选版本：`2026071013`。
- HTTP 已下载到偏移 `69632` 后停止 MQTT Broker。
- MQTT 中断 433 秒期间，HTTP Range 继续到最后一块 `1556480-1560575`。
- Broker 恢复后设备重新连接，并上报版本 `2026071013`。
- 最终 `asset.error="none"`、`http.error=""`。

证据目录：`tools/logs/w800_mqtt_interrupt_20260710_112048`。

这证明 MQTT 是控制/状态面，不是图片数据面的依赖。更新期间允许 MQTT 暂时离线，恢复后补报最终状态。

### inactive slot 写入期间 MCU 复位

- 基线版本：`2026071013`。
- 候选版本：`2026071014`。
- Range 进行到偏移 `131072` 后复位 MCU。
- 重启后 active version 仍为 `2026071013`，资源可用，decoder `readFail=0`。
- 重新触发后 451 秒完成 `2026071014`。

证据目录：`tools/logs/w800_update_reset_20260710_113010`。

### 10 次连续真实资源包更新

- 版本范围：`2026071015..2026071024`。
- 每次资源包：`1560576` 字节，总传输 `15605760` 字节。
- 10 次全部通过，无失败。
- 单次耗时：最短 456 秒，最长 477 秒，平均 466.1 秒。
- active slot：`1,0,1,0,1,0,1,0,1,0`，A/B 严格交替。
- 最终版本：`2026071024`，Slot A（`slot=0`）。
- 最终包 CRC32：`0xF63CF616`。
- AT binary：`19088/19088` 成功。
- header、capacity、raw-arm、timeout 错误均为 0。

证据目录：`tools/logs/w800_http_range_stress_20260710_115238`。

### LVGL 外部 Flash 运行

- 观察 66 秒，覆盖 3 个以上 20 秒翻页周期。
- 版本 `2026071024` 和 active slot 全程稳定。
- decoder 增量：`info +1023`、`open +510`、`area +2362`。
- `readFail=0`。
- 实机照片确认背景页、动态图层和自动切页均正常。

证据目录：`tools/logs/ui_asset_runtime_20260710_131238`。

### 防降级

- active version：`2026071024`。
- 尝试安装旧版本：`2026071023`。
- 设备拒绝并报告 `asset.error="old version"`、`http.error="begin"`。
- active version 和 slot 均未变化，服务器资源随后恢复到 `2026071024`。

证据目录：`tools/logs/ui_asset_rollback_20260710_131509`。

## AT Core 验收

主机测试 `tools/test_at_core_host.ps1` 覆盖：

1. 无额外分隔符且 payload 自身以 CR/LF 开头。
2. W800 header 后额外空行，payload 同时以 CR/LF 开头。
3. 非法长度字段。
4. 超容量 payload。
5. partial payload 超时。
6. 失败后下一条命令恢复。

结果：`PASS at_core binary framing: 6 cases`。

通用 `at_core` 不再硬编码 W800 的 header 空行；W800 driver 显式选择 `AT_RAW_SEPARATOR_EMPTY_LINE`，其他模组默认 `AT_RAW_SEPARATOR_NONE`。

## 构建与烧录验收

最终使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -WaitMqttSeconds 120
```

结果：

- pyOCD 只擦除并写入 `0x08020000-0x08200000`。
- Bootloader 区未被覆盖。
- program/verify 成功。
- reset 后 MQTT 上报匹配新固件 `fwBuildId`。
- MQTT 日志由 `1883` 监听进程定位，且只接受 reset 完成后的新状态。
- Keil 通过 `Start-Process -Wait` 同步执行，build log/HEX/AXF 必须属于本次构建；新 AXF 的 build ID 会自动成为 MQTT 精确匹配条件。

## 最终板端状态

最终清理复位后：

- `fwBuildId="Jul 10 2026 13:29:20"`。
- `asset.available=1`。
- `asset.version=2026071024`。
- `asset.slot=0`。
- `asset.error="none"`。
- decoder `readFail=0`。
- HTTP 状态机空闲、错误为空。
- AT binary `2/2`，各错误计数为 0。

实机照片还暴露出动态值刷新残影：外部 Flash 背景已经正常，但透明 label 的旧字符没有在局部刷新时可靠清除。`sim_ui.c` 已将每个动态字段改为固定尺寸、全不透明、且与资源遮罩同色的背景；该修复已包含在上述最终 build ID 中，不改变资源包或 HTTP/AT 链路。

## 验收边界

本轮已完成软件、实机链路和可重复故障注入验收。量产前仍应按产品规范另做长时间掉电循环、弱信号、AP 切换、Flash 擦写寿命、EMC/ESD 和异常电源波形测试；这些属于硬件与生产资格验证，不影响本轮功能闭环结论。
