# Message Bus Bridge Scope

`app_event_bridge` is the migration boundary between existing services and the optional Message Bus.

Current rule:

- LDC remains the owner of byte/block input, framing and packet queues.
- W800, RS485 and NearLink still consume packets through their original direct state machines.
- The bridge only publishes small metadata events when `APP_ENABLE_MSG_BUS` is enabled:
  - UART/link activity byte count.
  - Complete frame length notification.
  - Service status/state value.
  - Error code value.
- The bridge deliberately does not publish pointers to stack frame buffers. If a future handler needs payload data, add a static payload pool or make the original service own the payload lifetime explicitly.

This keeps the default direct path unchanged when the macro is off, while allowing new handlers to subscribe to bus events when device count grows.

New consumers should register through `app_msg_bus_service_subscribe()` instead of modifying the global bus directly. That keeps the subscription table protected by the same critical section used for publish/dispatch access.

The reusable Message Bus core now lives under `shared/comm/msg_bus`. This H563 application keeps only the board-specific service wrapper and bridge code.
