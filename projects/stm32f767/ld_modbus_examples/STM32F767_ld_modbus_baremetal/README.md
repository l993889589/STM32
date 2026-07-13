# STM32F767 ld_modbus 裸机示例

本工程基于 Issue 提交者提供的 STM32F767IGT6 CubeMX/Keil 工程直接移植，不使用作者自己的 BSP。CubeMX 继续负责时钟、GPIO、USART1、USART3 和中断初始化。

## 默认配置

- USART1：PA9/PA10，115200，保留原 `printf` 调试功能；
- USART3：PB10/PB11，RS232 点对点 Modbus RTU；
- 接收方式：`HAL_UART_Receive_IT(..., 1)` 单字节中断，不使用 DMA/IDLE；
- 默认角色：RTU Slave，地址 `1`；
- 默认波特率：115200，支持 9600、19200、115200；
- 全部协议缓冲区和寄存器静态分配，不使用堆内存；
- 程序从 `0x08000000` 启动。

## 配置入口

修改 `app/modbus_app_config.h`：

```c
#define MODBUS_APP_ROLE      MODBUS_APP_ROLE_SLAVE
#define MODBUS_APP_BAUD_RATE (115200U)
```

主站模式改为 `MODBUS_APP_ROLE_MASTER`。示例主站每秒向地址 1 发送一次 FC03，请求保持寄存器 0 和 1，适合配合 Modbus Slave 上位机测试。

从站模式支持 `ld_modbus` 已实现的 FC01/02/03/04/05/06/0F/10/16/17。保持寄存器 0..7 包含芯片标识、收发计数、错误计数及 T1.5/T3.5 诊断，可使用 Modbus Poll 读取。

四类数据表均提供按地址访问函数：

```c
uint16_t value;

ld_modbus_server_map_write_holding_register(&g_map, 10U, 1234U);
ld_modbus_server_map_read_holding_register(&g_map, 10U, &value);
```

完整 API 位于 `Middlewares/ld_modbus/include/ld_modbus_server.h`。线圈和保持寄存器提供 `read/write`；离散输入和输入寄存器提供 `read/set`，其中 `set` 只供本机应用更新采集值，并不会允许远程主站写入协议只读表。所有接口都会检查空指针和地址范围。

## RTU 时间

工程保持 HAL 标准 1 ms SysTick，不产生 20 kHz 的 50 us 中断。`modbus_port_time_us()` 读取 `HAL_GetTick()` 与 `SysTick->VAL`，得到微秒级字符完成时间戳，然后交给 `ld_modbus_rtu_framer` 严格处理 T1.5/T3.5。

该实现不占用 TIM2，也不使用 DWT。TIM2、ADC 和 CAN 配置保留自原 CubeMX 工程，但 Modbus 示例不会启动或依赖它们。

## 运行入口

CubeMX 初始化完成后：

```c
app_init();

while (1)
{
    app_poll();
}
```

USART3 中断仍由 CubeMX 生成的 `USART3_IRQHandler()` 转发给 HAL。`app/modbus_port.c` 实现 HAL UART 接收完成及错误回调。

## 编译

```powershell
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\test_build_matrix.ps1
```

或打开 `MDK-ARM/STM32F767_ld_modbus_baremetal.uvprojx`。复制工程使用 Arm Compiler 6.21；原附件指定 6.22，若本机已安装 6.22，可在 Keil Target 设置中切回。

## 验证边界

已使用 ArmClang 6.21 完成 Slave/Master × 9600/19200/115200 共 6 个配置的完整重编译，全部为 `0 Error(s), 0 Warning(s)`。默认 Slave 镜像占用：Code 21624、RO 592、RW 12、ZI 10068 字节。

当前只进行主机测试、静态检查和 Keil 重编译。没有连接、烧录、擦除、复位或运行 STM32F767 硬件；串口收发和 SysTick 插值仍需提交者在开发板上验证。
