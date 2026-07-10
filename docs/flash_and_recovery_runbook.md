# STM32H563 构建、烧录和恢复 Runbook

本文定义 W800/UI 图片更新主线中的固件烧录边界。日常开发只使用一个入口；恢复路径只在日常入口失败或板子进入不可用状态时使用。

## 日常入口

推荐命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build
```

该命令负责：

1. 调用 Keil `UV4.exe -r` 重新构建应用工程。
2. 使用 `Start-Process -Wait` 等待 UV4 真正退出，不能在 GUI 构建仍进行时继续烧录。
3. 检查 build log 中的 `0 Error(s)`，并验证 build log、HEX、AXF 的时间均晚于本次构建开始时间。
4. 打印即将烧录的 HEX/AXF 路径、时间、大小和从 AXF 提取的 `fwBuildId`。
5. 擦除完整应用区：

```text
0x08020000-0x08200000
```

6. 使用 pyOCD/CMSIS-DAP 写入应用 HEX。
7. reset 目标板，并要求 MQTT 上报与新 AXF 完全一致的 `fwBuildId`。

Bootloader 区域不被日常脚本擦写：

```text
0x08000000-0x0801FFFF
```

## 可选 MQTT 上线等待

如果 Desktop Debug Assistant 已运行，可以让脚本在 reset 后等待 W800 状态上报：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build -WaitMqttSeconds 120
```

如果已知期望固件构建 ID，也可以指定：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\STM32H563_App\flash_cmsis_dap.ps1" -Build -WaitMqttSeconds 120 -ExpectedFwBuildId "Jul  9 2026 20:03:43"
```

脚本会优先根据 TCP `1883` 的监听进程定位当前 release 的 `latest.log`，避免误读备份目录或旧 Electron 实例的日志。MQTT 验证时间门限以 pyOCD reset 完成时刻为准，只接受 reset 后的新状态；使用 `-Build` 时还会自动从新 AXF 提取 build ID 并要求精确匹配。因此旧心跳和旧 HEX 都不能通过上线验收。

注意：MQTT 上线等待只证明运行时 W800 任务已经重新连上，并不能替代 pyOCD program/verify。日常“烧录的是哪个文件”以脚本打印的 HEX/AXF 时间和路径为准。

2026-07-10 最终实机结果：

- Keil：`0 Error(s), 0 Warning(s)`。
- pyOCD program/verify 成功。
- 固件：`fwBuildId="Jul 10 2026 13:29:20"`。
- 资源 active version：`2026071024`。
- Bootloader 保护区未擦写。

本次同时修复了一个确定的竞态：直接使用 PowerShell `& UV4.exe` 时，GUI 进程可能在构建结束前把控制权返回脚本，导致脚本读取并烧录上一次 HEX。当前入口通过等待 UV4、检查 artifact 新鲜度和匹配 AXF build ID 三道门阻止该问题复现。

## 不再反复做的验证

不要每次都做整片 Flash readback。只有出现下列情况时才读回：

- 怀疑 app 写到了错误地址。
- 怀疑 bootloader 从外部 Flash 覆盖了 app。
- pyOCD 报 program/verify 错误。
- probe 选择错目标。

## Keil 直接下载

Keil `UV4.exe -f` 只作为恢复路径。已观察到 RDDI-DAP erase/program 失败，不能作为本阶段的唯一日常烧录方式。

可用命令：

```powershell
$uv4 = "C:\Keil_v5\UV4\UV4.exe"
$proj = "D:\Embedded\H5\STM32H563_App\MDK-ARM\STM32H563_Threadx_usbx_cdc_acm.uvprojx"
$target = "STM32H563_Threadx_usbx_cdc_acm"
& $uv4 -f $proj -t $target -o "D:\Embedded\H5\STM32H563_App\MDK-ARM\flash_from_keil.log"
```

如果 Keil 下载失败，回到日常 `flash_cmsis_dap.ps1 -Build` 或使用 USB OTA 恢复。

## USB OTA 恢复

当 app 仍能枚举 CDC ACM 且 bootloader 已正确部署时，可使用 USB OTA 写应用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Embedded\H5\flash_app_usb_ota.ps1" -Port COM4
```

该路径写外部 Flash OTA download slot 和 manifest，不直接擦写 bootloader。

## Bootloader 保护边界

当前 USB OTA 不更新 bootloader。bootloader 自身变更只能用以下恢复方式之一：

- SWD/CMSIS-DAP 或 ST-LINK。
- STM32CubeProgrammer。
- STM32 ROM DFU/USART bootloader，前提是 BOOT0 和接口可用。
- 工装夹具。

日常图片更新只使用 GD25LQ128 的 UI asset A/B 区：

```text
Slot A: 0x00500000-0x009FFFFF
Slot B: 0x00A00000-0x00EFFFFF
```

不要把 UI 图片包、OTA app 包或调试数据写到 bootloader 下载区。
