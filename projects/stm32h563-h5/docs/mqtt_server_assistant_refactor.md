# LeduO MQTT Server Assistant Refactor

日期：2026-07-01

## 目标

将原来的桌面调试助手收敛为完整 MQTT 服务器工具，删除串口、USB Vendor、OTA、板子烧录、485 本地压测等能力，只保留：

- 本地 MQTT Broker
- W800/STM32 设备状态解析
- 设备配置下发
- Modbus 数据上报展示
- MQTT 事件日志
- 本地日志文件

## 工程位置

- 源码：`D:\Embedded\备份\desktop-debug-assistant`
- 博客：`F:\3d-notes`

## 主要改动

### Electron 主进程

重写 `electron/main.js`，只保留：

- `mqtt:start`
- `mqtt:stop`
- `mqtt:status`
- `mqtt:publish`
- `mqtt:simulate-w800`
- `logs:paths`
- `logs:clear`

删除串口、USB Vendor、OTA、烧录流程相关 IPC 和实现。

### Preload

重写 `electron/preload.cjs`，只暴露 MQTT 和日志接口。

### 前端 UI

重写 `src/App.jsx`，保留原有暖色 + 青绿色工业工具视觉风格，页面改为三栏：

- 左侧：Broker 控制、局域网地址、日志文件路径
- 中间：MQTT 事件日志
- 右侧：发布消息、设备配置、设备状态、Modbus 数据、固件连接配置

### 样式

重写 `src/style.css`，去掉串口发送栏、终端、OTA、板子流程等布局。

### 依赖

从 `package.json` 删除：

- `serialport`
- `usb`
- `@xterm/xterm`
- `@xterm/addon-fit`

`npm install` 后实际移除 30 个包。

### 源码清理

删除不再使用的文件：

- `electron/vendor-usb.js`
- `electron/vendor-usb.test.js`
- `electron/vendor-protocol.js`
- `electron/vendor-protocol.test.js`
- `docs/ldc-stress-protocol.md`
- `scripts/tail-latest-log.ps1`

## 验证

构建通过：

```powershell
npm run build
```

构建结果：

- CSS：约 7.46 kB
- JS：约 157.66 kB

相比原先串口/OTA/烧录混合助手，前端包体明显下降。

## 博客自启

`F:\3d-notes\restart-vitepress.ps1` 已改为绑定所有网卡：

```powershell
$HostAddress = '0.0.0.0'
```

已创建当前用户 Startup 快捷方式：

```text
C:\Users\99388\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\LeduO Lab Blog VitePress.lnk
```

已立即启动并验证：

```text
http://127.0.0.1:5173/ -> 200
```

防火墙入站规则尝试失败，原因是系统要求管理员提升权限：

```text
The requested operation requires elevation (Run as administrator).
```

当前 VitePress 已监听 `0.0.0.0:5173`，如果局域网访问仍被防火墙拦截，需要管理员权限添加 TCP 5173 入站放行规则。
