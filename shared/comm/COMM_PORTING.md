# Communication Library Porting Guide

This directory is the reusable communication package. It is intentionally split into layers so small projects can keep the direct path and larger projects can enable a central message bus later.

## Layer split

```text
shared/ldc/
  ldc_core.c/.h
  ldc_packet.c/.h
  ldc_ring.c/.h

shared/comm/ldc_endpoint/
  threadx/ldc_endpoint_threadx.c/.h

shared/comm/msg_bus/
  app_msg_bus.c/.h
```

The LDC core files remain independent and should not depend on HAL, RTOS, UART, USB or Message Bus code.

## What to copy into another STM32 project

Minimal byte/block receiver and frame queue:

```text
shared/ldc/ldc_core.*
shared/ldc/ldc_packet.*
shared/ldc/ldc_ring.*
```

Event-driven task wakeup with ThreadX:

```text
shared/ldc/*
shared/comm/ldc_endpoint/threadx/*
```

Optional system event bus:

```text
shared/comm/msg_bus/*
```

The Message Bus core is still a pure C component. It does not call HAL, RTOS or interrupt APIs by itself. If a project publishes from ISR and receives from tasks, protect these operations in the platform service layer, as the H563 `app_msg_bus_service` does.

Application glue is not portable by design:

```text
STM32H563_App/user/app/app_msg_bus_service.*
STM32H563_App/user/app/app_event_bridge.*
STM32H563_App/user/app/app_ldc_config.*
```

Those files bind the reusable library to this board, ThreadX thread creation, shell, W800, RS485, USB and NearLink services.

## Recommended integration path

1. Bring up `shared/ldc` first and feed it from UART RX/DMA/USB receive callbacks.
2. If the project uses an RTOS, add an endpoint adapter so tasks block on events instead of polling every 1 ms.
3. Keep protocol parsers and state machines in application services.
4. Add `shared/comm/msg_bus` only when cross-module dependencies become hard to maintain.
5. Do not send large UART/USB payloads through the bus. Keep payload ownership in LDC/protocol buffers; publish only metadata, status, control events or pointers with a documented lifetime.

## Message Bus v1.1 features

The bus supports both the original positional initializer and the newer config initializer:

```c
app_msg_bus_init(...);
app_msg_bus_init_with_config(&bus, &config);
```

Use `app_msg_bus_init_with_config()` when you need queue full policies:

```c
config.high_full_policy = APP_MSG_DROP_OLDEST;
config.normal_full_policy = APP_MSG_DROP_NEWEST;
```

Supported full policies:

- `APP_MSG_DROP_NEWEST`: keep queued messages and reject the new one.
- `APP_MSG_DROP_OLDEST`: overwrite the oldest message in the selected queue.
- `APP_MSG_FORCE_HIGH_PRIORITY`: currently behaves as drop-oldest on the selected high-priority queue. The core does not move messages between high and normal queues.

`app_msg_t` includes lightweight metadata fields:

- `flags`: priority/ISR/DMA/urgent hints.
- `timestamp`: optional publisher-provided timestamp.
- `data` + `length`: pointer payload with externally managed lifetime.
- `value`: small scalar payload for status, counters and error codes.

## Current H563 mapping

```text
BSP UART/USB RX
  -> ldc_endpoint_write()
  -> LDC core framing
  -> endpoint event flags
  -> service task parser/state machine
  -> optional app_event_bridge metadata event
  -> optional app_msg_bus_service dispatcher
```

`APP_ENABLE_MSG_BUS` controls only the optional system event bus. It does not change the LDC receive/framing path.
