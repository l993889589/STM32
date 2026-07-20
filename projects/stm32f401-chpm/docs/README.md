# CHPM 文档索引

## 当前工程合同

- `BOARD_MANIFEST.md`：时钟、引脚、外设和唯一软件所有者。
- `RUNTIME_DATAFLOW.md`：ThreadX 线程、IRQ、Modbus、USB/DWIN、Flash 和传感器时序。
- `STORAGE_LAYOUT.md`：W25Q64 双扇区参数日志、提交和掉电恢复。
- `BUILD_AND_VALIDATION.md`：只编译命令、主机测试、验证结果和实机待测项。
- `VSCODE.md`：代码跳转、一键编译和工程配置同步。
- `THIRD_PARTY_NOTICES.md`（工程根目录）：第三方许可与 Git submodule 提交。

## 协议与实现说明

- `USB_CDC_DWIN_PROTOCOL_AND_RISK_REVIEW.md`：上位机私有协议、USB CDC、
  DWIN DGUS 帧、地址映射和链路风险总览。文档保留审查当时的风险描述；
  已处理项目应结合当前源码及后续实现文档阅读。
- `DWIN_TX_OWNER_IMPLEMENTATION.md`：唯一 DWIN TX owner、latest-wins、
  事件 FIFO 和蜂鸣器可靠调度。
- `DWIN_RX_LENGTH_FRAMING.md`：`5A A5 LEN` 连续解析、DMA 块交接和残帧恢复。

## 迁移与历史

- `MIGRATION.md`：从旧 FreeRTOS/USB Device/手写 Modbus 结构迁移到
  ThreadX、USBX、LDC 2.0.2 和 `ld_modbus` 的差异。
- `reference/F4_legacy_freertos_usb_device.ioc`：旧工程外设配置参考，不是
  当前 ThreadX/USBX 工程的生成入口。

所有 Markdown 使用 UTF-8。二进制手册、屏幕工程、构建输出和本机日志不进入
公开源码仓库；协议字段以当前源码和本目录文档为准。
