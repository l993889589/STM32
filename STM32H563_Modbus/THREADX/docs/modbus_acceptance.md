# Modbus 实机验收记录

日期：2026-07-11

## 测试链路

`desktop-debug-assistant -> COM3 USB-RS485 -> RS485-1/MAX13487 -> USART2 PA2/PA3`

串口为 115200 8N1，从站地址 1。H7-TOOL 通过 CMSIS-DAP/pyOCD 整片擦除、烧录、
校验和复位；所有 standalone 镜像及向量表从 `0x08000000` 开始。

## 四目标结果

| 镜像 | RX / TX | Keil | COM3 从站回归 |
| --- | --- | --- | --- |
| 裸机 IT | ReceiveToIdle IT / polling | 0 / 0 | 16/16 |
| 裸机 DMA | GPDMA Ch1 / GPDMA Ch2 | 0 / 0 | 16/16 |
| ThreadX IT | ReceiveToIdle IT / polling | 0 / 0 | 16/16 |
| ThreadX DMA | GPDMA Ch1 / GPDMA Ch2 | 0 / 0 | 16/16 |

16 项覆盖 FC01/02/03/04/05/06/0F/10/16/17、非法地址、非法功能、CRC 错误、
错误站号、广播无响应但生效，以及故障注入后的恢复。

## 板端 RTU 主机

向保持寄存器 60 写入 `0x4D53` 后，固件暂时切换到主机状态机并依次发送：

1. FC03 读取远端寄存器 0..1，验证身份 `0x4C44, 0x0001`。
2. FC06 写远端寄存器 5 为 `0x55AA`。
3. FC03 读回寄存器 5，确认写入值。

只有 CRC、站号、功能码和 PDU 解析均正确才推进状态。完成后自动恢复从站，寄存器
61..63 返回 `0x600D / 3 / 0`。裸机 IT 与 ThreadX DMA 均在 COM3 模拟远端从站时
通过，捕获的请求为 `010300000002C40B`、`0106000555AA26E4`、
`010300050001940B`。这同时证明 polling TX 与真正的 USART2 TX DMA 路径。

## W800 与边界

W800 Server 条件编译已在四目标通过，Client 条件编译已在裸机 IT 和 ThreadX DMA
通过；默认无凭据构建也进入所有目标。运行入口、独立静态表、掉线清理和重连路径均已
接入。尚未提供测试 SSID、密码和局域网端点，所以不声称板端 Wi-Fi/socket 实测通过。

本记录不验证产品 Boot 的签名、OTA 或回滚链；回迁约束见
`boot_app_reintegration.md`。
