# STM32H563 ld_modbus 验证工程

这是从 `0x08000000` 独立启动的 STM32H563 Modbus 参考工程，用于验证
`ld_modbus`、LDC、RS485-1、ThreadX 和 W800。它不经过产品 Bootloader，开发期间
没有修改 `STM32H563_App` 或 `STM32H563_Bootloader`。

## 四个可构建目标

| 运行环境 | USART2 RX | USART2 TX | Keil 目标 |
| --- | --- | --- | --- |
| 裸机 | ReceiveToIdle IT | polling | `STM32H563_Modbus` |
| 裸机 | GPDMA1 Channel 1 | GPDMA1 Channel 2 | `STM32H563_Modbus_DMA` |
| ThreadX | ReceiveToIdle IT | polling | `STM32H563_Modbus_ThreadX_IT` |
| ThreadX | GPDMA1 Channel 1 | GPDMA1 Channel 2 | `STM32H563_Modbus_ThreadX_DMA` |

四目标共享同一份协议、LDC、BSP、transport 和 app 源码。协议表、ADU、队列、环形
缓冲、DMA 缓冲、ThreadX 控制块和线程栈全部静态分配；`ld_modbus` 不调用堆、OS、
UART 或 socket API。

## RTU 主从与 T3.5

- RS485-1：USART2，PA2/PA3，MAX13487 自动方向；默认站号 1、115200 8N1。
- 从机支持 FC01/02/03/04/05/06/0F/10/16/17 和标准异常响应。
- `bsp_uart_get_config()` 返回实际串口参数；LDC 的 `ldc_serial_silence_us()` 只做
  通用字符时间换算；Modbus app 决定 `<=19200` 使用 3.5 字符、较高波特率使用
  固定 1750 us。超时与协议解析没有耦合。
- 向保持寄存器 60 写 `0x4D53` 可启动非阻塞 RTU 主机示例：FC03 身份读取、FC06
  写寄存器 5、FC03 读回，完成后恢复从站；61..63 暴露状态/步数/错误。

## W800 Modbus TCP

从产品 App 只复制静态 AT/W800 驱动源码，未搬运凭据，也未改原 App。网络服务支持：

- TCP Client：周期 FC03、MBAP 事务号和 unit id 校验。
- TCP Server：listener/child socket、完整 MBAP ADU 收发和独立静态寄存器表。
- 裸机 10 ms 有界轮询；ThreadX 独立静态网络线程；错误后关闭并延时重连。

默认 `MODBUS_W800_ENABLE=0`。将
`user/app/modbus_network_config_local.example.h` 复制为同目录下
`modbus_network_config_local.h` 并填入本地参数即可启用；该文件已被 Git 忽略。
Client/Server 启用分支均已编译验证，但没有测试网络凭据，因此当前不宣称 W800 实机
联网通过。

## 构建、主机测试与烧录

```powershell
cd D:\Embedded\H5\STM32H563_Modbus
.\check_project.ps1
.\tests\test_timing.ps1
.\build.ps1 -Variant All
.\build_threadx.ps1 -Variant All

# 会整片擦除内部 Flash；只用于 standalone 验证
.\flash.ps1 -Runtime ThreadX -Variant DMA
```

独立协议库测试：

```powershell
cd Middlewares\ld_modbus
cmake -S . -B build -G "MinGW Makefiles" -DLD_MODBUS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

COM3 实机测试：

```powershell
cd D:\Embedded\H5\desktop-debug-assistant
npm run test:modbus-hardware
npm run test:modbus-master-hardware
```

构建脚本要求 Keil `0 Error(s), 0 Warning(s)`，并检查 Load Region 与向量表均从
`0x08000000` 开始。产品 Boot/App 的 `0x08020000`、VTOR 和签名回迁步骤见
[`docs/boot_app_reintegration.md`](docs/boot_app_reintegration.md)，完整实机证据见
[`docs/modbus_acceptance.md`](docs/modbus_acceptance.md)。
