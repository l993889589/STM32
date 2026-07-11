# Modbus 实机验收记录

日期：2026-07-11

## 测试链路

```text
desktop-debug-assistant
  -> COM3 / USB-RS485
  -> 板载 RS485-1 / MAX13487
  -> USART2 PA2/PA3, 115200 8N1
  -> BSP ReceiveToIdle IT 或 DMA
  -> LDC T3.5 静默间隔分帧
  -> ld_modbus RTU 从站（站号 1）
```

H7-TOOL 通过 CMSIS-DAP/pyOCD 执行内部 Flash 整片擦除、烧录、校验和复位。
各测试镜像及向量表起始地址均为 `0x08000000`。

## 运行环境矩阵

| 镜像 | 构建 | COM3 RTU 回归 |
| --- | --- | --- |
| 裸机 + ReceiveToIdle IT | 0 error / 0 warning | 16/16（早期基线） |
| 裸机 + ReceiveToIdle DMA | 0 error / 0 warning | 16/16 |
| ThreadX + ReceiveToIdle IT | 0 error / 0 warning | 16/16 |
| ThreadX + ReceiveToIdle DMA | 0 error / 0 warning | 16/16 |

测试结束时板上保留 `STM32H563_Modbus_ThreadX_DMA` standalone 镜像。

## 每轮 16 项检查

1. FC03 读取身份保持寄存器。
2. FC04 读取输入寄存器。
3. FC06 写单个保持寄存器。
4. FC10 写多个保持寄存器。
5. FC16 掩码写寄存器。
6. FC17 写/读多个寄存器。
7. FC05 + FC01 单线圈写读。
8. FC0F + FC01 多线圈写读。
9. FC02 读取离散输入。
10. 非法地址返回异常码 02。
11. 非法功能返回异常码 01。
12. CRC 错误请求保持静默。
13. 错误站号请求保持静默。
14. 广播写不响应。
15. 广播写实际生效。
16. 故障注入后可继续完成正常请求。

## 验收边界

- RTU 实机结果证明 standalone 固件、LDC、RS485-1、IT/DMA 和 ThreadX 任务链路可工作；
  不验证产品 Boot 的签名、OTA 或回滚链路。
- W800 Modbus TCP Client/Server 已按官方 `SKCT`、`SKSTT`、`SKSND`、`SKRCV`
  接口完成静态代码集成，并在四个目标中编译通过；尚未配置真实 Wi-Fi 凭据做板端网络实测。
- 桌面端 TCP master/slave 和 `ld_modbus` TCP 编解码已有主机测试；板端 W800 网络验收需要
  单独提供测试 SSID、密码和局域网端点。
