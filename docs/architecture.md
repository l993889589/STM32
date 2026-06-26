# STM32H563 H5 当前架构

本文档是当前工程的权威架构说明。旧的乱码草稿、早期规则和 Message Bus 设想以本文为准。

## 工作区结构

```text
D:/Embedded/H5
  .vscode/                 VS Code 编译/烧录任务
  docs/                    项目自写文档
  shared/                  可复用通信库
  tests/                   主机侧测试
  STM32H563_App/           ThreadX + USBX 应用工程
  STM32H563_Bootloader/    Bootloader / OTA 安装工程
  GuixDemo/                GUIX 设计源文件，当前保留
```

## 分层原则

```text
STM32 HAL / CMSIS / USBX / ThreadX
        -> BSP
        -> shared communication libraries
        -> protocol / device services
        -> app services / shell
```

约束：

- BSP 只处理板级硬件绑定、GPIO、UART、SPI、LCD、复位脚、芯片选择脚等。
- BSP 不放协议状态机，不放业务流程。
- LDC 核心保持纯 C，不依赖 HAL、ThreadX、USBX。
- AT core 负责命令会话、响应、URC、命令计划。
- AT modules 负责具体模组差异，例如 W800、NearLink、EC20。
- App service 负责状态机、任务调度、shell 命令和设备组合逻辑。
- Message Bus 是可选上层事件路由，不替代 LDC。

## App 当前主要链路

```text
UART/USB RX
  -> BSP callback / USB service
  -> ldc_endpoint_threadx
  -> shared/ldc core
  -> app service task
  -> protocol/parser/state machine
```

具体服务：

- `app_usb_service`：USB CDC / vendor transport / OTA 数据入口。
- `app_rs485`：RS485 + Modbus RTU。
- `app_w800`：W800 AT + Wi-Fi/MQTT 状态机。
- `app_nearlink`：NearLink AT + 主从角色状态机。
- `app_shell`：shell 命令注册和状态查询。
- `app_msg_bus_service`：可选 Message Bus 服务包装。
- `app_event_bridge`：把现有服务的轻量状态事件桥接到 Message Bus。

## BSP 当前设备

- UART：
  - USART1：W800 AT。
  - USART2：RS485。
  - USART3：NearLink AT。
- SPI：
  - SPI1：GD25LQ128 SPI NOR。
  - SPI2：ST7796U2 LCD。
- GPIO：
  - PC12：状态 LED。
  - PC9：W800 reset。
  - PC7：NearLink reset。
  - PB4/PB11/PD11/PD12：LCD reset/backlight/CS/DC。
  - PB14/PB15：触摸 reset/int 预留。

## LCD/ST7796U2

LCD 驱动位于 `STM32H563_App/user/bsp/bsp_lcd.*`。

- 分辨率：480 x 320，RGB565。
- SPI：SPI2。
- 背光：PB11 普通 GPIO 高电平打开，暂不启用 PWM。
- 开机：`bsp_init()` 调用 `bsp_lcd_init()`，完成复位、初始化、清黑屏、点亮背光。
- 触摸：当前只配置 reset/int，I2C PB8/PB9 尚未启用。

## Bootloader / OTA

Bootloader 和 App 通过共享 OTA layout 协议协作。外部 SPI NOR 保存 OTA 包、manifest 和 rollback 数据。App 负责接收 OTA 包并写入外部 flash，Bootloader 负责安装、回滚和跳转。

当前文档只描述方向；以代码中的 `ota_layout.h`、`ota_boot.*`、`app_ota.*` 为最终实现依据。
