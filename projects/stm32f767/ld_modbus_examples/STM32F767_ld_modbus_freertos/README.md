# STM32F767 ld_modbus FreeRTOS 示例

本工程基于 Issue 提交者提供的 STM32F767IGT6 CubeMX/Keil 工程直接移植，不使用作者自己的 BSP。USART、GPIO、时钟和中断仍使用原 CubeMX/HAL 文件；FreeRTOS 只负责调度同一套 `app_init()`/`app_poll()` Modbus 服务。

## 默认配置

- USART1：PA9/PA10，115200，保留原 `printf` 调试功能；
- USART3：PB10/PB11，RS232 点对点 Modbus RTU；
- 接收方式：`HAL_UART_Receive_IT(..., 1)` 单字节中断和静态环形队列；
- 不使用 DMA、ReceiveToIdle、RS485 DE/RE、TIM 或 DWT；
- 默认角色：RTU Slave，地址 `1`；
- 支持 Slave/Master 与 9600、19200、115200；
- FreeRTOS Kernel V10.6.2，1 kHz SysTick；
- Modbus 任务、栈、TCB、Idle 任务和全部协议缓冲区均为静态内存；
- `configSUPPORT_DYNAMIC_ALLOCATION=0`，工程不编译任何 `heap_x.c`；
- 程序从 `0x08000000` 启动。

## FreeRTOS 运行边界

`freertos_app_start()` 创建一个静态 Modbus 任务，任务每 1 ms 调用一次 `app_poll()`。USART3 中断只记录字节、接收完成时间戳和诊断计数，不调用 FreeRTOS API，因此原 USART3 中断优先级可以保留。

`SysTick_Handler()` 每 1 ms 先调用 `HAL_IncTick()`，调度器启动后再调用 `xPortSysTickHandler()`。`modbus_port_time_us()` 同时读取 `HAL_GetTick()` 和 `SysTick->VAL`，得到微秒级字符完成时间戳，因此不需要把 SysTick 提高到 20 kHz，也不需要额外硬件定时器。

FreeRTOS 内核以源码方式放在 `Middlewares/Third_Party/FreeRTOS`。Keil 使用适配 ArmClang 6.21 的 Cortex-M7 GCC 风格端口；没有使用旧 ARMCC/RVDS 汇编端口。

## 角色和波特率

修改 `app/modbus_app_config.h`：

```c
#define MODBUS_APP_ROLE      MODBUS_APP_ROLE_SLAVE
#define MODBUS_APP_BAUD_RATE (115200U)
```

主站模式改为 `MODBUS_APP_ROLE_MASTER`。示例主站每秒向地址 1 发送一次 FC03，请求保持寄存器 0 和 1。默认从站支持 FC01/02/03/04/05/06/0F/10/16/17，保持寄存器 0..7 提供芯片标识、收发计数、错误计数和 T1.5/T3.5 诊断。

## 四类数据表访问

`Middlewares/ld_modbus/include/ld_modbus_server.h` 提供 8 个应用接口：

- Coil：`read/write`；
- Discrete Input：`read/set`；
- Holding Register：`read/write`；
- Input Register：`read/set`。

`set` 用于本机应用或传感器任务更新协议只读表，不会改变远程 Modbus 写权限。接口检查空指针与地址范围、不静默返回伪数据，也不分配内存。若多个任务同时访问同一数据表，应用需要在调用边界提供同步。

## CubeMX 说明

随工程保留 `STM32F767_ld_modbus_freertos.ioc`，它记录提交者原有的芯片、时钟和外设配置。FreeRTOS 内核采用手工源码集成，没有向 `.ioc` 写入 CubeMX 中间件元数据。若重新生成工程，请保留 USER CODE 区，并确认 Keil 工程仍包含 `freertos_app.c`、`tasks.c`、`list.c` 和 Cortex-M7 `port.c`。

## 编译与检查

```powershell
powershell -ExecutionPolicy Bypass -File .\check_project.ps1
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\test_build_matrix.ps1
```

也可以打开 `MDK-ARM/STM32F767_ld_modbus_freertos.uvprojx`。工程默认使用 Arm Compiler 6.21；原附件指定 6.22，若本机已安装 6.22，可自行在 Keil Target 设置中切换并重新验证。

已完成 Slave/Master × 9600/19200/115200 共 6 个配置的完整重编译，全部为 `0 Error(s), 0 Warning(s)`。默认 Slave 镜像占用：Code 24924、RO 724、RW 16、ZI 14024 字节。

## 验证边界

当前只完成宿主测试、静态检查和 Keil 重编译。没有连接探针，没有烧录、擦除、复位或运行 STM32F767 硬件。USART3 RS232 收发、FreeRTOS 任务运行和 SysTick 微秒插值仍需在开发板上验证。
