# 开发工作流

## VS Code 编译和烧录

VS Code tasks 已配置在 `.vscode/tasks.json`。

常用入口：

- `Ctrl + Shift + B`：执行默认任务 `Keil: Rebuild App`，只编译不烧录。
- `Ctrl + Shift + P` -> `Tasks: Run Task`：
  - `Keil: Rebuild App`
  - `Keil: Flash App`
  - `Keil: Rebuild + Flash App`
  - `Keil: Flash App (No Verify)`

底层脚本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File STM32H563_App/flash.ps1 -Build -BuildOnly
powershell -NoProfile -ExecutionPolicy Bypass -File STM32H563_App/flash.ps1 -Build
```

## 主机侧测试

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

覆盖范围包括：

- LDC。
- AT command plan。
- Message Bus。
- USB vendor protocol。
- shell。
- libmodbus 相关测试。

## OTA 包

OTA 包是生成产物，不作为必要源码保留。需要时重新生成：

```powershell
powershell -ExecutionPolicy Bypass -File make_ota_package.ps1
```

默认输出目录是 `ota_package/`。

## 提交流程

- 每次功能改动后编译验证。
- 只暂存本次相关文件。
- 不把构建产物、临时目录、外部调试助手、node_modules、release 包提交进固件工程。
- 如果工作区已有历史脏改动，提交时必须确认 staged diff，不用 `git add .`。
