# H5 工程文档入口

本目录是 H5 工作区内项目自写 Markdown 文档的唯一存放位置。源码目录、测试目录和根目录不再散落项目设计文档，避免旧设计和当前实现互相冲突。

## 当前有效文档

- [architecture.md](architecture.md)：当前 STM32H563 App / Bootloader / BSP / LDC / AT / Message Bus 分层架构。
- [communication-porting.md](communication-porting.md)：LDC、endpoint、Message Bus 复用和移植边界。
- [message-bus.md](message-bus.md)：可选 Message Bus 的真实状态、效率边界和启用规则。
- [usb.md](usb.md)：USB CDC、Vendor Bulk、OTA/压力测试通道规划。
- [issue-log.md](issue-log.md)：已定位问题和后续处理状态。
- [workflow.md](workflow.md)：VS Code、Keil、烧录、测试、提交流程。
- [stm32-communication-architecture.md](stm32-communication-architecture.md)：STM32 通信架构设计手册，解释 LDC、ISR、endpoint、Message Bus、BSP/CubeMX 的取舍。

## 清理规则

- `STM32H563_App` 和 `STM32H563_Bootloader` 是当前固件工程，保留。
- `shared` 是可复用通信库源码，保留。
- `tests` 是主机侧单元测试，保留。
- `docs` 只放项目自写文档。
- HAL、USBX、GUIX、libmodbus 等第三方目录自带的 README/LICENSE 不移动。
- 构建产物、OTA 包、桌面调试助手、旧草稿目录不作为 H5 固件工程必要内容保留。
