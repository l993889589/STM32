# 通信库移植说明

本工程把通信能力拆成三个层级，方便以后移植到其他 STM32 工程。

## 1. 最小 LDC 核心

只需要字节/块接收、ring buffer、packet FIFO 和 framing 时，复制：

```text
STM32H563_App/user/ldc/core/ldc_core.*
STM32H563_App/user/ldc/core/ldc_packet.*
STM32H563_App/user/ldc/core/ldc_ring.*
```

这些文件必须保持：

- 不依赖 HAL。
- 不依赖 RTOS。
- 不依赖 UART、USB、AT、Modbus、Message Bus。
- 不使用动态内存。

## 2. RTOS endpoint

如果目标工程使用 ThreadX，并希望任务不再固定 1 ms 轮询，可以复制：

```text
service-local LDC binding in STM32H563_App/user/app/*
```

endpoint 的职责是把 LDC 的输入/成帧事件转换成 ThreadX event flag。它不是 LDC core 的一部分。

## 3. 可选 Message Bus

当外设数量多、模块之间依赖复杂时，可以复制：

```text
Message Bus removed from active H563 app
```

Message Bus 只传轻量事件：

- 类型。
- 来源。
- 标志。
- 小整数值。
- 指针 + 长度，但指针生命周期必须由发布方保证。

不要把大块 UART/USB 数据复制进 bus。大数据仍留在 LDC、协议缓冲区或设备服务内部。

## H563 工程特有胶水

这些文件不作为通用库直接移植：

```text
STM32H563_App/user/app/app_ldc_config.*
STM32H563_App/user/app/app_msg_bus_service.*
STM32H563_App/user/app/app_event_bridge.*
```

它们绑定了本板子的 UART、USB、ThreadX、shell、W800、Console 和 RS485。

## 推荐移植顺序

1. 先移植 `STM32H563_App/user/ldc/core`，从 UART RX/DMA/USB RX 喂入数据。
2. 如果需要低延迟唤醒，再加 endpoint 适配层。
3. 协议解析和设备状态机放在目标工程 app/service 层。
4. 只有当模块数量增加、依赖开始混乱时，再启用 Message Bus。
5. 保持 BSP、通信库、协议、业务状态机的依赖方向单向。
