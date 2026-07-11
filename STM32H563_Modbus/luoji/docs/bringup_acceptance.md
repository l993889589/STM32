# 第一阶段 Bring-up 与验收约束

## 1. 第一阶段定义

第一阶段目标是建立“可移植 BSP 核心闭环”，不是完成产品应用。完成时必须证明：

> 同一套 BSP、LDC、外设驱动和服务核心，在不依赖 CubeMX 生成代码的情况下，可由裸机和 ThreadX 两个 target 构建并在当前 H563 板上完成关键链路验证。

第一阶段只在满足本文件 exit gate 后结束。单纯“能编译”“能点灯”或“ThreadX 工程能运行”都不代表完成。

### 1.1 当前执行授权（2026-07-10）

用户已把裸机范围扩展为“按当前两份原理图完成板载外设 BSP”，同时明确限定为 compile-only：

- 后续外设实现只进入 `luoji`，`THREADX` 保持上一编译基线，不同步本轮源码；
- 允许完成 ST7796、FT6336U、双 FDCAN、USB Device PCD、RTC、W800 控制和 GD25LQ128 擦写等底层；
- 禁止烧录、下载、擦除、复位、连接或运行目标板；
- 本轮 exit gate 是项目自有注释/边界静态检查与 Keil `0 Error(s), 0 Warning(s)`；
- 下文所有 Hardware gate 继续保留为未来验收要求，本轮不得把它们标为通过。

## 2. 开始实现前的入口条件

以下文档必须经评审，并在实现过程中保持一致：

- `docs/bsp_architecture.md`
- `docs/board_resource_manifest.md`
- `docs/clock_and_memory_contract.md`
- `docs/bringup_acceptance.md`

允许未解决问题存在，但必须满足：

- 不影响第一阶段所实现资源；
- 明确标为 `provisional` 或 `open_issue`；
- 不被实现代码默认为已验证事实；
- 有后续验证方法。

## 3. 第一阶段交付范围

### 3.1 构建和目录骨架

必须交付：

- 固定版本 vendor CMSIS/HAL；
- 平铺的 `user/bsp`，以 `bsp_*`、`board_*` 和 `mcu_*` 区分逻辑边界；
- `user/bsp` 中的 GD25LQ128E 最小驱动；
- `user/ldc` 的固定版本；
- bare-metal 和 ThreadX 两个 OSAL backend；
- 独立的 `luoji` 工程；
- 独立的 `THREADX` 工程；
- Keil 构建 target 和可审查的 linker/memory 配置；
- host tests 和静态检查脚本。

构建输入不得依赖 `.ioc`、`.mxproject` 或任何 `MX_*` 函数。CubeMX 仅作为最初的时钟工程生成器。

项目自有 `.c/.h` 必须通过文件头和函数注释静态检查；vendor、ThreadX 与固定版本 LDC 使用上游注释，不纳入重写。

### 3.2 启动、时钟和基础设施

必须实现并验证：

- reset cause capture；
- 早期安全 GPIO；
- 25 MHz HSE 到 250 MHz SYSCLK；
- voltage scale、Flash latency/program delay；
- ICACHE/DCACHE 初始化；
- 裸机 SysTick HAL timebase，以及 ThreadX 工程私有的 TIM17 HAL timebase；
- DWT 微秒时间；
- wrap-safe deadline；
- critical-section save/restore；
- error stage/status 和 fault snapshot；
- clock query service。

### 3.3 GPIO 和板级初始化

必须实现：

- `board_init()` 分阶段返回状态；
- status LED；
- Flash CS 安全高；
- Display CS 安全高；
- LCD backlight 默认关闭；
- W800、LCD、Touch 控制脚的安全状态只使用已冻结极性；未冻结项不得做破坏性切换；
- board resource compile-time conflict checks 的第一版。

### 3.4 PWM

必须实现通用物理量 API：

- 输入 logical role、frequency 和 duty；
- 自动查询 timer clock；
- 自动计算 PSC/ARR/CCR；
- 返回 achieved frequency/duty/error；
- 显式处理 0%/100%；
- 检测共享 timer base 冲突；
- 运行中更新保持安全、可预测。

实机使用 PB11/TIM2_CH4 LCD 背光验证，不要求第一阶段实现完整 LCD。

### 3.5 UART、DMA/IRQ 和 LDC

机制必须覆盖四个逻辑 UART 角色：

- Debug/USART3；
- Wi-Fi/USART1；
- RS-485 1/USART2；
- RS-485 2/UART4。

第一阶段最低实机验证：

- Debug UART 完成稳定收发；
- 至少一条 RS-485 完成收发或可控回环；
- ReceiveToIdle/producer-position 事件可持续工作；
- ORE、DMA error、overflow 后可有界恢复；
- ISR 不解析 LDC/协议；
- Debug 使用 newline delimiter 的 LDC 实例；
- RS-485 使用微秒 gap 的 LDC 实例；
- 裸机和 ThreadX 复用同一 UART BSP 与 LDC core；
- W800 只验证 transport 机制，不进入完整 AT/MQTT；其 LDC 配置必须保留 mixed stream 不使用 delimiter 的约束。

如果第一阶段只给 USART1 使用 DMA，其余 UART 使用中断 ReceiveToIdle，必须在诊断和接口层保持相同语义，并记录性能限制。不能为了形式统一而在未验证 Cache/通道分配时强行给所有 UART 上 DMA。

### 3.6 SPI Flash

必须实现：

- SPI1 总线初始化；
- PA4 CS 安全控制；
- GD25LQ128E reset/release（如器件流程需要）；
- JEDEC ID 读取；
- bounded transfer 和 busy polling；
- SPI bus arbitration 接口；
- 裸机和 ThreadX 共用设备驱动，仅 OSAL 锁行为不同；
- timeout、IO error、retry/recovery 计数。

不要求第一阶段实现完整擦写压力测试、FileX、LevelX 或 OTA。

### 3.7 裸机与 ThreadX

裸机 target 必须：

- 通过有界 superloop 调用本工程的 `poll/step`；
- 不依赖 RTOS 头文件或对象；
- 在等待期间按契约推进必要服务；
- 能运行与 RTOS 相同的 clock、PWM、UART/LDC 和 Flash 验证。

ThreadX target 必须：

- BSP 公共层无 `tx_api.h`；
- task、stack、event、mutex 静态分配；
- OSAL 是公共服务看到的唯一 ThreadX 适配；
- ISR 使用 `_from_isr` 通知路径；
- interrupt priority 满足 ThreadX port 约束；
- 能运行与裸机相同的关键验证。

## 4. 原第一阶段不包含项与当前边界

下列项目原本不作为第一阶段完成条件。当前裸机 compile-only 扩展已实现其中的板级底层，但没有扩大到产品业务层：

- ST7796 基础初始化、窗口与 RGB565 传输已实现；GUIX/LVGL 未内置；
- FT6336U 复位、ID 和双触点读取已实现；校准/手势业务属于上层；
- 双 FDCAN 物理时序求解、accept-all 硬件过滤、静态收发、错误计数和显式恢复已实现；产品 ID 白名单属于 service；
- USB Device PCD、PMA 与 endpoint API 已实现；USBX/CDC/HID/vendor class 未内置；
- W800 GPIO 时序和 UART 角色已实现；AT session、网络和 MQTT 未内置；
- Modbus master/server 业务；
- GD25LQ128 读、页写和 4 KiB 擦除已实现；FileX/LevelX 文件系统未内置；
- OTA、bootloader 跳转和镜像认证；
- TrustZone/secure boot；
- 低功耗模式；
- EMC、环境、寿命、功能安全或工业认证。

这些资源仍需保留在 board manifest 中，且上电安全状态不能因“不实现”而忽略。

## 5. 实施顺序和阶段门

### Gate 0：文档冻结

通过条件：

- 四份约束文档无相互矛盾；
- 当前板资源均有 owner、必需性、安全状态和证据状态；
- 第一阶段的 unresolved item 不阻塞所选验证链路。

### Gate 1：纯构建骨架

通过条件：

- 两个 target 都能编译链接空 bring-up；
- 无 `.ioc`/`MX_*` 构建依赖；
- vendor 版本、board、runtime、memory profile 选择唯一；
- map 文件中 Flash/RAM origin 正确。

### Gate 2：启动与安全

通过条件：

- HSE/PLL/Cache/timebase 正常；
- SYSCLK 验证为 250 MHz；
- reset/fault 诊断可读取；
- 上电时 CS、背光、reset 等处于文档安全状态；
- 调试器在冷启动和故障后仍可连接。

### Gate 3：基础 I/O 与 PWM

通过条件：

- LED 行为和 active-low 语义正确；
- Debug UART baud 实测通过；
- PB11 PWM 的频率和 duty 实测通过；
- PWM host solver 边界测试通过。

### Gate 4：UART/LDC 运输闭环

通过条件：

- Debug LDC newline frame 正常；
- RS-485 gap frame 正常；
- 连续流、wrap、overflow、ORE 和 restart 测试有结果；
- ISR 最大工作量有界；
- bare-metal/ThreadX 使用语义一致的独立源码副本；
- 诊断 counters 可读取。

### Gate 5：SPI Flash 闭环

通过条件：

- JEDEC ID 与 GD25LQ128E 预期一致；
- CS 时序和 SPI mode 经逻辑分析或等效方法验证；
- busy/transfer timeout 可故障注入；
- 两个 runtime target 结果一致。

### Gate 6：第一阶段退出

通过条件：

- 下节完整矩阵通过；
- 未测试项与失败项单独列出；
- 文档与最终实现一致；
- 换板规则没有被共享层中的当前板硬编码破坏。

## 6. 验收矩阵

| 类别 | 检查项 | 必须结果 |
| --- | --- | --- |
| Build | `stm32_h563_baremetal` | 编译、链接、启动通过 |
| Build | `stm32_h563_threadx` | 编译、链接、启动通过 |
| Warnings | 编译警告 | 逐项审查，不全局屏蔽 |
| Static | `MX_`/CubeMX 依赖 | 项目自有源码为零 |
| Static | BSP/LDC 中 `tx_`/`TX_` | 为零；OSAL/target integration 除外 |
| Static | app/protocol 直接 HAL/寄存器 | 为零 |
| Static | duplicate vector/HAL callback | 为零 |
| Static | runtime heap | BSP/LDC/基础驱动为零 |
| Host | clock/PWM solver | 边界、舍入、冲突通过 |
| Host | deadline wrap | 通过 32-bit wrap 测试 |
| Host | LDC/ring | wrap、overflow、delimiter、gap 通过 |
| Hardware | 安全 GPIO | 冷启动/复位均符合清单 |
| Hardware | SYSCLK/timebase | 测量与 250 MHz/1 ms 契约一致 |
| Hardware | UART baud | 测量在约定误差内 |
| Hardware | PWM | frequency/duty 测量在报告误差内 |
| Hardware | UART continuous RX | 无静默覆盖；异常可恢复并计数 |
| Hardware | RS-485 | 自动方向行为正确，无虚构 DE 控制 |
| Hardware | SPI Flash | JEDEC ID 和有界失败路径通过 |
| Hardware | Fault | 保存 fault/reset 诊断，安全输出不失控 |
| Memory | linker map | origin、section、对齐、容量均审查 |
| Memory | DMA/Cache | ownership transfer 和一致性测试通过 |

## 7. 量化验收建议

具体阈值在实现前由 target config 固化，第一版建议：

- PWM frequency error：在可实现范围内优先不超过 0.1%，并报告实际值；
- PWM duty：按 timer 分辨率报告量化误差；
- UART baud error：满足 STM32H5 UART 和外部设备容差，并记录实际 BRR/误差；
- 所有 blocking API：必须接收 timeout 或使用文档固定的短有界 timeout；
- UART overflow/ORE/DMA error：不能静默，100% 进入计数器；
- SPI Flash busy polling：必须有总 deadline，不按单次 HAL timeout 无限重试；
- ThreadX stacks：启用 stack error 检测并记录 high-water 或等效数据；
- 所有静态 buffers：尺寸、对齐和 owner 在 map/manifest 中可追踪。

阈值不能只写在测试脚本里，必须在 target config 或公共契约中有单一来源。

## 8. 必须保留的证据

每次 Gate 验收保存：

- toolchain/HAL/CMSIS 版本；
- target、board revision、memory profile；
- clean build log 和 warnings；
- linker map/size；
- host test 结果；
- 静态扫描结果；
- 示波器/逻辑分析仪测得的 clock、baud、PWM、SPI 证据；
- 实机序列号或板卡标识；
- fault injection 和恢复结果；
- 未测试项、失败项和硬件假设。

源码架构通过不等同于 EMC、可靠性、网络安全、功能安全或工业认证通过。

## 9. 后续硬件验收与上层候选顺序

在获得明确的烧录/连接授权后，建议按以下顺序继续；当前 compile-only 任务不执行这些项目：

1. 逐项检查早期安全电平、HSE/LSE、SysTick 和调试可连接性；
2. 测量 UART、SPI、I2C、PWM 与 FDCAN 物理时序；
3. 读取 GD25LQ128、FT6336U 实际 ID，确认 ST7796 方向/色序/背光极性；
4. 验证双路 RS-485 与 FDCAN 收发、错误注入和 bus-off 恢复；
5. 挂接并验证所选 USB device/class stack；
6. 根据实测吞吐冻结 GPDMA request、静态 buffer section 与 Cache 策略；
7. 再实现 W800 AT/session、Modbus、FileX/LevelX、bootloader/OTA 等上层；
8. watchdog supervisor、低功耗和长期压力测试强化。

第二阶段仍保持同一 BSP/OSAL/LDC 边界，不允许按功能再次复制底层目录。
