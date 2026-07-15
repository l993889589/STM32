# ART-Pi Gateway Studio

ART-Pi Gateway Studio 是 ART-Pi STM32H750 工业网关的 Qt 6/QML 桌面客户端。板端负责 ThreadX、NetX Duo、RS485/Modbus 调度和实时数据采集，桌面端通过 HTTP API 完成监控、配置、历史记录、告警和维护，不复制板端的实时状态机。

当前桌面端版本为 `0.4.0`，配套板端固件版本为 `0.2.0`、HTTP API 版本为 `2`。

## P0-P3 能力

### P0：可靠连接与配置

- 区分连接中、在线、降级和离线状态，显示连续失败次数、固件版本与 API 版本。
- 配置提交前在本机校验；HTTP 4xx 时优先显示板端返回的 `field`、`reason` 和 `error`。
- 只提交已启用从机的配置，保存成功后才关闭配置窗口。
- 支持 1~10 个 Modbus 从机、四类寄存器、轮询周期、离线探测、红灯和蜂鸣器参数。
- 支持高优先级写线圈、写保持寄存器命令，并显示命令执行结果。

### P1：工业监控与数据

- 工艺总览图显示 ART-Pi、RS485/Modbus 总线与 10 个从机的在线状态和工程值。
- SQLite 点表支持寄存器映射、比例、偏移、单位和上下限报警。
- 每 5 秒采样，历史数据默认保留 7 天；支持 24 小时趋势和 CSV 报表导出。
- 告警具备产生、恢复、确认生命周期，并与审计记录关联。

### P2：诊断、版本与多网关

- 显示以太网、RS485、CRC、超时、DMA、LDC 丢帧和重连等诊断计数。
- 客户端日志持久化，默认最多保留 5000 条。
- 支持板端配置快照的版本保存、加载与删除。
- 支持多个网关地址档案并一键切换当前网关。

### P3：维护、安全与协议出口

- SQLite 数据库备份、维护统计和 24 小时 CSV 报表。
- 可选本机用户体系，提供 Viewer、Operator、Engineer、Administrator 四级角色。
- 内置 MQTT 3.1.1 客户端，以 QoS 0 周期发布板卡状态和工程点值。
- MQTT 当前为明文 TCP，适合受控局域网；尚未实现 TLS、QoS 1/2 或断线缓存。
- 当前不伪装成网络 OTA：固件升级仍通过 ST-LINK 或后续签名维护包完成。

## 数据位置

正常运行时数据库位于：

```text
%LOCALAPPDATA%\Leduo Lab\ART-Pi Gateway Studio\gateway_studio.db
```

数据库保存点表、历史样本、告警、审计、配置版本、网关档案、用户和持久日志。MQTT 密码只保留在本次进程内，不写入数据库或设置文件。

本机安全未初始化时应用按本机管理员模式运行。初始化管理员后，密码至少 8 个字符；该能力用于单机离线权限隔离，不替代 Windows 域、企业身份系统或专用凭据保险库。

## 构建环境

- Windows 10/11 x64
- Visual Studio 2022 C++ 工具链
- CMake 3.24 或更高版本
- Ninja
- Qt 6.8.3 MSVC 2022 64-bit，默认路径 `D:\Qt\6.8.3\msvc2022_64`
- Python 3，仅 MQTT 回环测试需要

推荐使用包装脚本，它会自动加载 Visual Studio 开发环境：

```powershell
cd D:\Embedded\STM32H750_ART\artpi_gateway_studio
.\build.ps1 -Configuration Release -Clean
```

可选配置为 `Release`、`RelWithDebInfo` 和 `Debug`。也可以直接使用 CMake Preset：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --clean-first
```

## 验证

板卡连接在 `192.168.1.20` 时执行完整回归：

```powershell
.\test.ps1 -Endpoint http://192.168.1.20
```

该脚本依次验证：

- 真实板卡 `/api/status` 与 `/api/config`；
- 配置写回和 Modbus 命令入队；
- SQLite 点表、采样、告警、审计、版本、网关、用户、备份和报表；
- MQTT CONNECT/CONNACK/PUBLISH 回环报文；
- 工业中心五个页面的 QML 加载与截图。

没有连接板卡时可跳过硬件接口：

```powershell
.\test.ps1 -SkipHardware
```

常用单项测试：

```powershell
.\build\release\artpi_gateway_studio.exe --self-test http://192.168.1.20
.\build\release\artpi_gateway_studio.exe --integration-test http://192.168.1.20
.\build\release\artpi_gateway_studio.exe --industrial-self-test
.\build\release\artpi_gateway_studio.exe --mqtt-self-test
.\build\release\artpi_gateway_studio.exe --ui-smoke-test
```

## 生成便携版

```powershell
.\build.ps1 -Configuration Release -Clean -Package
```

输出文件：

```text
build\release\ART-Pi-Gateway-Studio-0.4.0-win64.zip
```

ZIP 已包含 Qt 运行库、QML 模块、SQLite 驱动和 MSVC 运行时。解压后直接运行 `artpi_gateway_studio.exe`，目标电脑不需要安装 Qt SDK。

## 板端接口边界

桌面端依赖板端 `GET /api/status`、`GET /api/config`、`POST /api/config` 和命令接口。配置错误必须由板端返回结构化 JSON；桌面端负责把具体字段和原因呈现给用户。实时轮询、掉线退避、命令优先级和 Modbus 帧处理始终留在 STM32 端。
