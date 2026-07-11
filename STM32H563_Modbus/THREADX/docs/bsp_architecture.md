# STM32 H5 BSP 架构约束

> 打包例外：根据用户的独立复制要求，裸机与 ThreadX 各自包含完整 BSP，不使用顶层 shared 源码。本文的分层和 API 约束分别在每个工程内部执行；同步规则见 `packaging_contract.md`。
>
> 当前实施：Modbus 裸机和 ThreadX 使用语义一致、分别编译的完整源码副本；任何一边的公共接口变更都必须评审同步并分别构建。

## 1. 文档目的

本文定义 `STM32_H5_BSP` 的长期架构边界。目标不是为每个例程复制一套 BSP，而是建立一套：

- 不依赖 CubeMX 生成源码、`.ioc` 或 `MX_*` 初始化函数；
- 更换板卡后主要修改平铺目录中的 `board_config.h` 与 `board_*.c`；
- 裸机与 ThreadX 使用相同公共 BSP、设备驱动和协议代码；
- 应用层不接触 HAL 句柄、GPIO 端口、IRQ 名称和 DMA 通道；
- 所有等待、缓冲区、恢复路径和资源所有权都有明确边界；
- 项目自有名称统一使用 `lowercase_snake_case`。

本文是实现和评审的强制约束。偏离本文必须在变更记录中说明原因、影响和替代验证。

## 2. 当前目标矩阵

| 项目 | 第一阶段约束 |
| --- | --- |
| MCU | STM32H563RI，LQFP64 |
| 当前板 | `dshan_h563_industrial`，核心板加工业扩展板，原理图日期 2024-01-30 |
| 裸机 | 必须支持，使用有界 superloop/poll/step 模型 |
| RTOS | 必须支持 ThreadX；公共 BSP 不得包含 ThreadX 类型 |
| 工具链 | 第一阶段以 Keil MDK-ARM/Arm Compiler 6 为主 |
| HAL | 固定 CubeMX 时钟种子工程的 STM32H5 HAL 1.5.1，升级必须单独评审 |
| 启动模式 | `standalone` 与预留 `boot_app` 内存配置分离 |
| 安全域 | 第一阶段为 Non-Secure 单域；TrustZone 不进入第一阶段 |
| 动态内存 | BSP、LDC、基础驱动运行期禁止动态分配 |

第二块板出现时必须新增独立 board 目录，不能覆盖当前板绑定。

## 3. 目录和依赖方向

```text
luoji/
    Core/
    Drivers/
    MDK-ARM/
    user/
        bsp/          # 平铺 BSP
        ldc/
        osal/
        transport/

THREADX/
    Core/
    Drivers/
    MDK-ARM/
    AZURE_RTOS/
    Middlewares/ST/threadx/
    user/             # 完整独立副本，不引用 luoji
```

允许的依赖方向为：

```text
app
  -> services / protocol / transport
  -> osal public api
  -> bsp public api
  -> bsp board composition
  -> bsp mcu mechanisms
  -> vendor cmsis/hal
```

具体职责：

- `Drivers`：未经修改的 CMSIS、设备头文件和固定版本 HAL/LL。
- `BSP/Common`（`user/bsp/bsp_*`）：状态码、health、时钟查询、时间、IRQ lock 和公共外设 API。
- `BSP/MCU`（`user/bsp/mcu_*`）：STM32H5 HAL/CMSIS 私有适配，不作为应用接口。
- `BSP/Board`（`user/bsp/board_*`）：引脚、AF、极性、安全电平、实例、IRQ/DMA 分配和初始化组合。
- `BSP/Device`（`user/bsp/<chip_model>.*`）：以芯片型号直接命名的片外器件实现。
- `user/ldc`：字节流分帧库，不属于 BSP，不包含 HAL 或 RTOS 依赖。
- `osal`：时间、休眠、临界区、事件通知和有意共享资源的互斥适配。
- `transport/protocol/services`：数据运输、分帧使用、Modbus/AT 等协议与产品服务。
- `targets`：选择一块板、一种运行模式、一个内存布局和所需功能集合。

物理目录继续平铺在 `user/bsp`，Keil 分组只表达逻辑边界：`mcu_*` 不得依赖 `board_*`，Device 不得直接读取 `board_config.h`，应用不得包含 `mcu_*` 私有头文件。`system_stm32h5xx.c` 属于 `Core/System`。

## 4. 公共接口规则

### 4.1 状态域

所有可能失败的初始化和操作必须返回统一、可诊断的状态，至少覆盖：

- `BSP_STATUS_OK`
- `BSP_STATUS_ALREADY_INITIALIZED`
- `BSP_STATUS_INVALID_ARGUMENT`
- `BSP_STATUS_NOT_READY`
- `BSP_STATUS_BUSY`
- `BSP_STATUS_TIMEOUT`
- `BSP_STATUS_IO_ERROR`
- `BSP_STATUS_OVERFLOW`
- `BSP_STATUS_CONFLICT`
- `BSP_STATUS_NOT_SUPPORTED`
- `BSP_STATUS_DEGRADED`

不得以 `void` 隐藏关键初始化失败，也不得在请求被裁剪或替换后仍返回成功。

### 4.2 逻辑角色

板层以上只使用逻辑角色，例如：

- `BOARD_UART_DEBUG`
- `BOARD_UART_WIFI`
- `BOARD_UART_RS485_1`
- `BOARD_UART_RS485_2`
- `BOARD_PWM_LCD_BACKLIGHT`
- `BOARD_SPI_FLASH`
- `BOARD_SPI_DISPLAY`
- `BOARD_I2C_TOUCH`
- `BOARD_FDCAN_FIELD_1`
- `BOARD_FDCAN_FIELD_2`

`USART3`、`TIM2_CH4`、GPIO、AF、DMA 请求等只允许出现在 board 和 STM32H5 实现边界内。

### 4.3 API 语义

公共接口应优先接收物理量：

- 频率使用 `frequency_hz`；
- 波特率使用 `baud_rate`；
- 微秒和毫秒分别使用 `_us`、`_ms`；
- PWM 占空比使用明确固定比例，例如 `duty_permille`；
- 超时参数必须说明 `0` 的行为；
- 硬件存在舍入时返回实际达到值。

公共头文件不得暴露：

- `GPIO_TypeDef`、`UART_HandleTypeDef`、`TIM_HandleTypeDef` 等 HAL 类型；
- IRQn、DMA 控制器/通道和 STM32 寄存器；
- `TX_*`、`tx_*` 或其他 ThreadX 对象；
- 某块板的具体引脚宏。

### 4.4 生命周期

按模块适用范围使用统一动词：

```text
init -> configure -> start -> read/write/poll/step -> stop -> deinit
```

初始化必须幂等，或显式返回 `BSP_STATUS_ALREADY_INITIALIZED`。ISR 专用接口必须以 `_from_isr` 结尾。

### 4.5 文件头与函数注释

- 每个项目自有 `.c/.h` 文件必须以包含 `@file` 和 `@brief` 的文件头开始；
- 每个项目自有函数声明和定义前必须有文档注释；
- 公共函数必须说明参数、单位、返回值、阻塞/ISR 语义、所有权和重要副作用；
- 私有函数至少说明用途；非显然参数、时序和副作用必须展开说明；
- CMSIS、HAL、ThreadX 和固定版本 LDC 保持上游源码与注释，不为格式统一而修改。

## 5. 资源与所有权

每个下列资源必须只有一个可变所有者：

- 外设实例；
- 引脚和复用功能；
- DMA 请求/通道；
- IRQ 向量和 HAL 全局回调；
- 定时器 base frequency 及各通道；
- 总线仲裁状态；
- 驱动上下文、接收环、发送队列和诊断计数器。

一条 SPI/I2C 总线只能有一个仲裁所有者。设备驱动不能为了自己重新初始化共享总线。一个定时器的多个通道共享 PSC/ARR；不同通道请求不兼容频率时必须返回冲突。

向量表和 HAL 全局 callback 由 STM32H5 中断路由模块集中定义，再根据静态注册的资源所有者分发。禁止多个无关模块各自定义同一个 HAL callback。

## 6. 初始化顺序和失败分类

固定初始化顺序为：

```text
复位原因捕获
-> 最早期安全 GPIO
-> 电源和系统时钟
-> Flash 延迟、Cache 和内存策略
-> 单调时间、故障记录和诊断
-> MCU 外设机制
-> 板级总线和必需器件
-> 可选器件
-> transport/protocol/services
-> 启动裸机 superloop 或 RTOS kernel
```

安全 GPIO 必须在可能驱动外部器件之前建立，例如 Flash/LCD CS 拉高、背光关闭、复位脚进入确定状态。

失败分为：

- `stop`：主时钟、内存、必需安全输出或必需调试通道失败；进入定义好的安全状态并记录阶段/错误。
- `degraded`：可选器件不可用；系统继续运行并提供诊断。
- `retryable`：外部设备暂未就绪；按有界次数和退避策略重试。

禁止依赖不可观察的死循环作为唯一错误处理方式。

## 7. 裸机与 ThreadX 共用模型

公共服务写成有界状态机：

```text
service_init
service_step
service_next_deadline
service_on_event
service_get_health
```

- 裸机由 superloop 调用 `step/poll`。
- ThreadX task 只负责调度、等待和唤醒同一套状态机。
- ISR 只清中断、捕获位置/时间/错误并通知 owner，不解析协议、不打印、不动态分配、不普通加锁。
- BSP 公共调用在两种模式下语义一致。
- OSAL 临界区必须保存并恢复进入前的中断屏蔽状态，不能盲目重新开中断。

`tx_api.h` 只允许出现在 `osal/osal_threadx.c` 和 ThreadX target/application integration 中。

## 8. LDC 使用契约

LDC 的权威基线为 GitHub `l993889589/STM32` 中 H563 工程使用的完整副本，当前审计提交：

```text
21658fcca87a23ed8c604f580940abb7259b5370
```

本地 `D:/Embedded/H5/STM32H563_App/user/ldc/core` 与该提交内容一致。使用规则：

- `libraries/ldc` 保持独立，不放入 `bsp/mcu` 或 board 目录；
- LDC core/easy 不包含 HAL、ThreadX、malloc 和全局产品策略；
- UART BSP 只上报字节块、时间戳和错误事件；
- consuming service 持有自己的 `ldc_easy_t`、ring 和 packet pool；
- Debug/shell 可使用换行 delimiter；
- RS-485 使用微秒级帧间隔；
- W800 的 AT 文本与二进制混合流保持 delimiter 关闭，由上层解析器切换 line/raw 模式；
- LDC 的锁和通知通过注入接口或 OSAL adapter 提供；
- BSP 的 1 ms tick ISR 不直接调用产品级 `ldc_easy_tick_all()`，由 service 根据单调时间推进对应实例。

后续应把完整 H563 LDC 提升为 GitHub 顶层 `libraries/ldc` 的唯一规范版本，避免顶层旧副本与工程副本分叉。

## 9. 第一阶段实现边界

第一阶段必须实现：

- 两个独立目标：`stm32_h563_baremetal`、`stm32_h563_threadx`；
- 启动、时钟、Cache、复位/故障、单调时间；
- 当前板安全 GPIO 和完整资源登记；
- 通用 TIM/PWM 求解器，并用 LCD 背光验证；
- 四个 UART 角色的统一机制，至少验证 Debug 和一条 RS-485 数据链路；
- UART 接收事件到 LDC 的裸机/ThreadX 共用链路；
- SPI1 和 GD25LQ128E JEDEC ID；
- OSAL bare-metal/ThreadX 后端；
- 静态诊断、超时和 host tests。

第一阶段暂不实现完整 LCD/LVGL、触摸业务、FDCAN 服务、USBX、W800 AT/MQTT、Modbus 业务、FileX/LevelX、OTA、bootloader 或 TrustZone。这些资源仍必须提前登记并建立安全状态。

## 10. 命名约束

- 目录、文件、函数、变量、成员和项目自有类型使用 `lowercase_snake_case`；
- 类型以 `_t` 结尾；callback 类型以 `_cb_t` 结尾；
- enum value 和宏使用 `UPPERCASE_SNAKE_CASE`；
- 名称包含单位后缀；
- 公共符号不得使用 `HAL_`、`LL_`、`MX_`、`tx_` 等供应商命名空间；
- 供应商原始标识只在 vendor/STM32 实现边界内保留；
- Keil 同一 target 内避免重复源文件 basename。

## 11. 架构验收条件

发生以下任一情况，架构评审不通过：

- 换 UART 实例或引脚需要修改应用、协议或 LDC；
- 裸机和 ThreadX 出现两套 BSP/协议实现；
- 公共头文件泄漏 HAL/ThreadX 类型；
- 工程仍依赖 `.ioc`、CubeMX 生成初始化或 `MX_*`；
- DMA 缓冲区没有静态存储、对齐和 Cache 策略；
- ISR 中出现协议解析、日志、无界循环或普通 mutex；
- 关键等待没有 timeout；
- 资源清单中仍有矛盾项却开始实现对应驱动。
