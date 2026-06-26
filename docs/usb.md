# USB 架构说明

USB 当前目标是把人机 shell、二进制通道和 OTA 数据通道分开，避免 CDC 同时承担日志、shell、OTA、大流量压力测试。

## 当前职责

- USBX / STM32 DCD：枚举和底层 endpoint 传输。
- `usb_console`：CDC 实例和串行化输出。
- `shell`：行编辑、命令解析、命令分发。
- `app_usb_service`：USB 数据入口，优先处理 OTA magic frame，再处理 shell 或 LDC 数据。
- `usb_vendor_transport`：vendor bulk 传输封装。
- `usb_vendor_protocol`：vendor frame 编解码。

## CDC

CDC 面向人工操作：

- shell 输入输出。
- 简短日志。
- 状态查询。

CDC 不应该承载高频二进制压力测试和大 OTA 数据。

## Vendor Bulk

Vendor Bulk 面向结构化二进制流：

- OTA。
- LDC 压力测试。
- 二进制命令。
- 结构化日志。

当前 vendor frame 格式以代码中的 `usb_vendor_protocol.*` 为准。

## 后续原则

- CDC 和 vendor bulk 使用独立队列/worker。
- OTA 通道保留 CRC、sequence、length 校验。
- 大量日志不应在小栈 USBX 线程里直接格式化输出。
- 长期目标是把 CubeMX 生成的 USB glue 从业务层隔离出去，但不重写 HAL PCD、USBX 和 ThreadX。
