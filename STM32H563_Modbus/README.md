# STM32H563 ld_modbus 验证工程

这是一个从 `0x08000000` 独立启动的 STM32H563 Modbus 工程，用来验证
`ld_modbus`、LDC、RS485-1 和 W800。它不经过产品 Bootloader，也不会修改
`STM32H563_App` 或 `STM32H563_Bootloader` 的源码。

## 已有目标

| 运行环境 | UART RX | Keil 目标 |
| --- | --- | --- |
| 裸机 | ReceiveToIdle IT | `STM32H563_Modbus` |
| 裸机 | ReceiveToIdle DMA | `STM32H563_Modbus_DMA` |
| ThreadX | ReceiveToIdle IT | `STM32H563_Modbus_ThreadX_IT` |
| ThreadX | ReceiveToIdle DMA | `STM32H563_Modbus_ThreadX_DMA` |

四个目标使用同一份 `ld_modbus`、LDC、BSP 和应用服务代码。协议表、ADU、LDC
队列、UART 环形缓冲、ThreadX 控制块和线程栈全部静态分配。

ThreadX 目标为 Cortex-M33 Non-Secure 单镜像，SysTick 只归 ThreadX；HAL 的
1 ms 时基由 TIM17 提供。Modbus 任务每 1 ms 调用一次有界的
`modbus_app_step(1000)`。

## RTU 与串口

- RS485-1：USART2，PA2/PA3，MAX13487 自动方向控制。
- 默认：从站地址 1，115200、8N1。
- `bsp_uart_get_config()` 返回实际波特率、数据位、校验和停止位。
- `ldc_serial_silence_us()` 只负责通用字符时间换算，不依赖 Modbus。
- Modbus 应用在 `<= 19200` 时按实际字符宽度计算 T3.5；更高波特率按规范使用
  固定 1750 us。
- DMA 接收使用 GPDMA1 Channel 1，区分 HT、IDLE、TC，按偏移搬运，避免半传输
  与空闲事件重复入队；同时记录错误、溢出和重启计数。

## Modbus TCP 与 W800

`user/at` 是从 `STM32H563_App` 搬入的静态 AT 核心和 W800 socket 驱动副本，原
App 未修改。`transport_w800_tcp` 负责 USART1、AT 会话、Wi-Fi 入网、TCP 流的
MBAP 定长组帧，以及 Client/Server socket 生命周期。

- 主机：`modbus_tcp_w800_client_*` 编码事务号和 MBAP，发送调用者构造的 PDU，
  并校验响应事务号和单元号。
- 从机：`modbus_tcp_w800_server_*` 创建 W800 TCP listener，通过 `SKSTT` 获取接入
  的子 socket，调用 `ld_modbus_server_process_tcp_adu()` 后回复。
- SSID、密码、远端地址均由调用者传入，仓库不包含实际凭据。

W800 server 命令依据联盛德《WM_W800_SDK_AT 指令用户手册 V1.1》的
`AT+SKCT`/`AT+SKSTT` 定义：
https://www.winnermicro.com/upload/1/editor/1655369712096.pdf

## 构建与检查

```powershell
cd D:\Embedded\H5\STM32H563_Modbus

# 静态规则与 T3.5 主机测试
.\check_project.ps1
.\tests\test_timing.ps1

# 裸机 IT + DMA
.\build.ps1 -Variant All

# ThreadX IT + DMA
.\build_threadx.ps1 -Variant All
```

构建脚本要求 Keil 输出 `0 Error(s), 0 Warning(s)`，并检查 Load Region 和
`__Vectors` 均从 `0x08000000` 开始。

## 烧录

```powershell
# 只构建指定镜像
.\flash.ps1 -Build -BuildOnly -Runtime BareMetal -Variant IT
.\flash.ps1 -Build -BuildOnly -Runtime ThreadX -Variant DMA

# H7-TOOL 整片擦除、烧录、校验和复位
.\flash.ps1 -Runtime BareMetal -Variant DMA
.\flash.ps1 -Runtime ThreadX -Variant IT
```

`flash.ps1` 会整片擦除内部 Flash。当前镜像是 standalone 测试镜像；以后恢复产品
Boot + App 时，必须重新烧录 Boot，并把 App 的链接地址、VTOR 和签名契约恢复为
产品配置。

## RTU 实机回归

电脑 USB-RS485 接板载 RS485-1，当前使用 COM3：

```powershell
cd D:\Embedded\H5\desktop-debug-assistant
npm run test:modbus-hardware
```

原裸机 IT 固件已通过 16 项 RTU 实机检查，详见
`docs/modbus_acceptance.md`。DMA 与 ThreadX 镜像需要在 H7-TOOL 接入后分别烧录，
复用同一套验收脚本。
