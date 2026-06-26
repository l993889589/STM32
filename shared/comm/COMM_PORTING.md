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
