# ART-Pi Gateway Studio

ART-Pi Gateway Studio 是 ART-Pi STM32H750 工业网关的 Qt 6/QML 桌面客户端。
它直接使用板端已有 HTTP API，不在桌面端复制 RS485/Modbus 调度状态。

## 当前能力

- 连接到可配置的 ART-Pi IPv4/HTTP 地址，默认 `http://192.168.1.20`。
- 每秒读取板卡、以太网、RS485 和 1~10 个 Modbus 从机状态。
- 动态显示在线、退避、轮询、失败次数、下次调度时间和四类寄存器值。
- 读取并保存主从角色、从站数量、轮询周期、离线探测周期和每台设备的地址范围。
- 向板端优先命令队列提交写线圈/写保持寄存器命令。
- 保留最近 200 条客户端连接、配置和命令日志。
- 提供无界面的 `--self-test` 模式，便于对真实板卡做接口回归。

## 构建

依赖 Qt 6.8（Core、Gui、Network、Quick、QuickControls2）、CMake 和 Ninja。

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

## 真机自检

```powershell
.\build\release\artpi_gateway_studio.exe --self-test http://192.168.1.20
```

自检会并行读取 `/api/status` 与 `/api/config`，两者均通过 JSON 和字段校验后返回 0；不会修改板端配置，也不会下发 Modbus 写命令。

需要覆盖配置保存与命令入队时，使用集成测试。它会把当前配置原样写回，再向第 1 台设备写入线圈值 0：

```powershell
.\build\release\artpi_gateway_studio.exe --integration-test http://192.168.1.20
```

QML 运行期检查和界面抓图：

```powershell
.\build\release\artpi_gateway_studio.exe --ui-smoke-test
.\build\release\artpi_gateway_studio.exe --screenshot .\artpi_gateway_studio.png
.\build\release\artpi_gateway_studio.exe --page 2 --screenshot .\commands.png
.\build\release\artpi_gateway_studio.exe --open-config --screenshot .\config.png
```
