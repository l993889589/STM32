# Desktop Debug Assistant 后续任务规划

日期：2026-07-01

## 当前结论

- 源码位置：`D:\Embedded\备份\desktop-debug-assistant`
- 发布包参考：`D:\Embedded\H5\desktop-debug-assistant`
- MQTT Broker 已在助手中实现，入口位于 `electron/main.js` 的 `mqtt:*` IPC。
- W800 联网页面已有 Broker 启停、Topic 发布、设备状态卡、MQTT 事件日志。
- “配置页面”还没有独立成型，目前只是手工发布 JSON/文本命令。
- 助手板子流程目前调用 `D:\Embedded\H5\flash_ota_all.ps1`。
- `flash_ota_all.ps1` 当前仍使用 STM32CubeProgrammer/STLINK 逻辑。
- App 工程已有 DAPLINK/pyOCD 脚本：`D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1`。

## 目标

让助手成为 W800 + STM32H563 + 双路 Modbus 的统一调试入口：

1. 通过 DAPLINK 完成编译、烧录、复位。
2. 通过 MQTT 读取设备状态、配置和 Modbus 数据。
3. 通过配置页面下发 UART2/UART4 启停、从站地址、寄存器地址、轮询周期等参数。
4. 保留串口 Shell、USB OTA、Modbus 本地压测能力。

## 任务 1：统一烧录脚本到 DAPLINK

优先级：最高

### 改造点

- 新增或改造 `D:\Embedded\H5\flash_ota_all.ps1` 参数：
  - `-ProbeType stlink|daplink|auto`
  - `-AppOnly`
  - `-BootOnly`
  - `-Frequency`
  - `-Probe`
- 当选择 DAPLINK 时：
  - 使用 pyOCD。
  - 复用 `STM32H563_App\flash_cmsis_dap.ps1` 里的 target、pack、probe list、frequency 逻辑。
  - Bootloader 和 App 都能单独烧录。
- 当选择 STLINK 时：
  - 保留现有 CubeProgrammer 流程。
- `auto` 模式：
  - 先检测 CMSIS-DAP。
  - 找不到再尝试 STLINK。

### 验收

- 助手点击“编译+烧录+复位”时不需要打开 Keil。
- DAPLINK 能在助手输出窗口显示 probe、build、flash、reset 的完整日志。
- 目标连接失败时显示明确提示：probe 已发现但 SWD No ACK、pack 缺失、hex 缺失、build failed。

## 任务 2：助手板子流程增加烧录方式选择

优先级：高

### 改造点

- `src\App.jsx`
  - “板子流程”页增加 Probe 下拉：
    - Auto
    - DAPLINK/CMSIS-DAP
    - STLINK/CubeProgrammer
  - 增加频率输入，默认 `1000000`，可快速切到 `100000`。
  - 增加 AppOnly 选项，日常调试默认只烧 App。
- `electron\main.js`
  - `board:flash` IPC 将 ProbeType/Frequency/AppOnly/BootOnly 参数传给 PowerShell。
- `electron\preload.cjs`
  - 保持 `runBoardFlash(options)`，只扩展 options。

### 验收

- UI 上能选择 DAPLINK。
- 失败日志能直接显示在助手右侧输出区域。
- 不影响现有 OTA、串口、MQTT 功能。

## 任务 3：定义 W800 配置协议

优先级：高

### 推荐 Topic

| 方向 | Topic | 用途 |
| --- | --- | --- |
| STM32/W800 -> PC | `leduo/w800/status` | 在线、IP、RSSI、固件、运行时间 |
| STM32/W800 -> PC | `leduo/w800/config` | 当前配置快照 |
| STM32/W800 -> PC | `leduo/w800/modbus/data` | Modbus 最新数据 |
| PC -> STM32/W800 | `leduo/w800/cmd` | 命令和配置下发 |

### 推荐命令

```json
{"cmd":"status"}
{"cmd":"get_config"}
{"cmd":"set_ports","ports":"2"}
{"cmd":"set_ports","ports":"4"}
{"cmd":"set_ports","ports":"both"}
{"cmd":"set_ports","ports":"off"}
{"cmd":"set_poll","port":2,"slave":1,"fc":3,"addr":0,"count":3,"period":1000}
{"cmd":"save_config"}
{"cmd":"reboot"}
```

### 兼容策略

- 短期设备侧已支持 `mbctl:` 文本命令，可以先由助手把表单转换成：
  - `mbctl:ports=both`
  - `mbctl:poll=2,1,3,0,3,1000`
- 中期设备侧补 JSON 解析后，再直接下发 JSON。

## 任务 4：新增“设备配置”页面

优先级：高

### 页面入口

- 现有右侧工具页签从 5 个扩展为 6 个：
  - Modbus 指令
  - LDC 压测
  - W800 联网
  - 设备配置
  - OTA
  - 板子流程

### 页面内容

- 连接状态：
  - MQTT Broker 状态
  - W800 clientId
  - 最近一次状态时间
- RS485 端口模式：
  - 只启用 UART2
  - 只启用 UART4
  - UART2 + UART4 都启用
  - 全部关闭
- 轮询表格：
  - port：2 或 4
  - slave：1-247
  - fc：03 或 04
  - addr：寄存器起始地址
  - count：寄存器数量
  - period：轮询周期 ms
  - enabled：启停
- 操作按钮：
  - 读取配置
  - 应用到设备
  - 保存配置
  - 恢复默认

### 验收

- 表单输入能生成正确命令。
- 下发命令后 MQTT 日志可看到 publish-local。
- 设备上报 `config` 后页面自动刷新当前值。

## 任务 5：解析设备上报并驱动页面状态

优先级：中

### 改造点

- 扩展 `parseW800DeviceMessage(event)`：
  - 识别 `leduo/w800/config`
  - 识别 `leduo/w800/modbus/data`
  - 保留 `status` 解析。
- 新增状态：
  - `deviceConfig`
  - `modbusData`
  - `lastConfigAt`
  - `lastDataAt`
- W800 设备卡只显示摘要，详细配置放到“设备配置”页。

### 验收

- `simulateW800Device()` 增加 config/data 模拟上报。
- 不接真板子也能在页面看到配置表和数据表刷新。

## 任务 6：设备侧配置持久化

优先级：中

### 改造点

- STM32 侧增加配置结构：
  - 端口启用位图。
  - 每路轮询项。
  - 版本号、CRC。
- 先放 RAM，调通后再落 Flash/文件系统。
- 增加命令：
  - `get_config`
  - `set_ports`
  - `set_poll`
  - `save_config`
  - `load_config`

### 验收

- 断电前保存，重启后恢复 UART2/UART4 轮询配置。
- 配置损坏时回退默认值。

## 任务 7：端到端验收流程

优先级：最高

### 流程

1. 启动助手开发版：`npm run dev`
2. 启动 MQTT Broker。
3. 用 DAPLINK 执行编译+烧录+复位。
4. 等待 USB CDC 重连。
5. 等待 W800 连接 MQTT。
6. 在“设备配置”页选择：
   - UART2 工作
   - UART4 工作
   - 双路工作
7. 分别下发轮询配置：
   - UART2：slave 1，fc 03，addr 0，count 3，period 1000
   - UART4：slave 1，fc 03，addr 0，count 3，period 1000
8. 检查：
   - MQTT config 上报。
   - MQTT modbus/data 上报。
   - Shell `modbus status` 与页面一致。

### 阻塞风险

- 当前现场 DAPLINK 可枚举，但 SWD 曾出现 No ACK，需要确认目标供电、SWDIO/SWCLK/GND、BOOT0、NRST 和读保护状态。
- 如果仍 No ACK，软件侧最多只能确认 probe 和脚本链路，无法完成烧录验收。

## 执行顺序

1. 先改 `flash_ota_all.ps1` 支持 DAPLINK。
2. 再改助手 Board 页和 IPC 参数。
3. 增加配置协议文档。
4. 增加设备配置页面。
5. 增加 MQTT config/data 解析和模拟数据。
6. 接设备侧 JSON 或兼容 `mbctl:` 命令。
7. 最后跑 npm build、助手 dev 联调、DAPLINK 烧录、MQTT 端到端验收。
