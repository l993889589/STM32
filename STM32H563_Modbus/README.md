# STM32H563 Modbus 独立测试工程

本工程把 STM32H563 工业扩展板作为一块全新的独立板运行，用于验证
`ld_modbus`、LDC 和 RS485-1。当前不经过原 Bootloader。

## 当前硬件配置

- MCU：STM32H563RIV6，25 MHz HSE，系统时钟 250 MHz。
- RS485-1：USART2，PA2/PA3，115200 8N1。
- 收发器：MAX13487 自动方向控制，不存在 DE GPIO。
- Modbus RTU 从站地址：1。
- 静态存储：协议栈、LDC 队列、ADU 缓冲区和寄存器表均不使用堆。
- 保持寄存器 0/1：`0x4C44`（“LD”）和协议版本 `0x0001`。

## Flash 布局

当前测试布局固定为：

```text
向量表 / 镜像起始地址：0x08000000
可用 Flash 长度：      0x00200000
```

`flash.ps1` 会使用 H7-TOOL（CMSIS-DAP/pyOCD）整片擦除，再执行烧录、
校验和复位。执行该脚本会删除内部 Flash 中原有的 Boot 和 App。

以后恢复产品 Boot + App 时，必须把应用链接地址和 VTOR 改回
`0x08020000`，重新走签名/镜像契约；不能直接把当前 standalone 镜像当作
Boot App 使用。

## 构建与烧录

```powershell
# 只做 Keil clean rebuild 和静态检查
.\flash.ps1 -Build -BuildOnly

# 使用现有 HEX 整片擦除并烧录
.\flash.ps1

# 重新构建后整片擦除并烧录
.\flash.ps1 -Build
```

构建脚本要求 Keil 输出 `0 Error(s), 0 Warning(s)`，并验证 Load Region 和
`__Vectors` 均从 `0x08000000` 开始。

## 实机验收

电脑 USB-RS485 适配器使用 COM3：

```powershell
cd D:\Embedded\H5\desktop-debug-assistant
npm run test:modbus-hardware
```

当前已通过 16 项实机检查，详见
[`docs/modbus_acceptance.md`](docs/modbus_acceptance.md)。
