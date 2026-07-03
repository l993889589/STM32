# W800 Network Configuration And Dual Modbus Master Plan

## Background

The current application has W800 Wi-Fi/MQTT support and a single RS485 path bound to USART2. That RS485 path is still shaped as a Modbus RTU slave example. The target architecture is:

- W800 Wi-Fi is the normal network channel for status, data, and configuration.
- W800 BLE is reserved for provisioning and local maintenance.
- USART2 and UART4 are two independent RS485 Modbus RTU master ports.
- Network configuration selects whether USART2, UART4, or both ports poll downstream devices.
- Poll targets include slave address, function code, register address, count, data type, and polling period.

## Scope

This plan covers the implementation path until the following seven checkpoints are complete:

1. Add UART4 to board initialization and BSP logical UART mapping.
2. Replace the single RS485 assumption with two Modbus master-capable RS485 logical ports.
3. Add Modbus RTU master request/response helpers.
4. Add a dual-port polling service with runtime status and result cache.
5. Add network-readable device status, configuration, poll data, and statistics.
6. Add network-writable configuration for port enable mode and poll target tables.
7. Build, flash, and verify the firmware without requiring manual Keil or serial-console work.

## Architecture

```text
W800 Wi-Fi / MQTT
  |
  v
app_w800 command handling
  |
  v
app_device_api
  |
  +-- app_modbus_master_get_status()
  +-- app_modbus_master_get_config()
  +-- app_modbus_master_apply_config()
  +-- app_modbus_master_get_data()
       |
       v
app_modbus_master
  |
  +-- port 0: BSP_UART_RS485_1 -> USART2
  +-- port 1: BSP_UART_RS485_2 -> UART4
```

## Configuration Model

Static memory is used throughout.

```c
typedef enum {
    APP_MB_PORT_2 = 0,
    APP_MB_PORT_4 = 1,
    APP_MB_PORT_COUNT
} app_mb_port_id_t;

typedef struct {
    uint8_t enabled;
    uint32_t baudrate;
    uint8_t parity;
    uint8_t stop_bits;
    uint16_t response_timeout_ms;
    uint16_t inter_request_gap_ms;
} app_mb_port_config_t;

typedef struct {
    uint8_t enabled;
    uint8_t port_id;
    uint8_t slave_addr;
    uint16_t poll_period_ms;
} app_mb_device_config_t;

typedef struct {
    uint8_t enabled;
    uint8_t device_index;
    uint8_t function;
    uint16_t reg_addr;
    uint16_t reg_count;
    uint8_t data_type;
    uint8_t word_order;
} app_mb_poll_item_t;
```

Initial limits:

- 2 Modbus ports.
- 16 downstream devices.
- 64 poll items.
- 16 registers per poll item.

## Network Topics

Device publishes:

- `leduo/{device_id}/status`
- `leduo/{device_id}/config`
- `leduo/{device_id}/modbus/data`
- `leduo/{device_id}/modbus/stats`

Device subscribes:

- `leduo/{device_id}/cmd/get_status`
- `leduo/{device_id}/cmd/get_config`
- `leduo/{device_id}/cmd/set_config`
- `leduo/{device_id}/cmd/reboot`

The first implementation may use compact JSON-like command payload parsing to avoid adding a dynamic JSON dependency on the MCU.

## Checkpoints

### 1. UART4 And BSP Mapping

- Add `huart4` declaration and initialization.
- Add `MX_UART4_Init()` call in startup.
- Add `UART4_IRQHandler()`.
- Add board pins according to CubeMX/current board configuration.
- Add `BSP_UART_RS485_1` and `BSP_UART_RS485_2`.
- Bind USART2 to `BSP_UART_RS485_1` and UART4 to `BSP_UART_RS485_2`.

Validation:

- Firmware builds.
- BSP can register RX callbacks and transmit through both logical ports.

### 2. Dual RS485 Port Ownership

- Stop treating RS485 as a singleton.
- Each Modbus port owns independent RX buffer, LDC endpoint, counters, and task state.
- Keep USART2 default-enabled for backward bring-up.
- UART4 can be disabled by default until hardware is verified.

Validation:

- Existing USART2 behavior still initializes.
- UART4 being disabled does not block boot.

### 3. Modbus RTU Master Helpers

- Build request frames for function code 03 and 04.
- Verify response slave address, function code, byte count, register count, and CRC.
- Add timeout and exception counters.
- Avoid dynamic allocation.

Validation:

- Host tests cover frame generation, CRC, normal response, exception response, and malformed response.

### 4. Polling Service

- Add `app_modbus_master`.
- Poll enabled items on enabled devices and enabled ports.
- Cache latest register values, last update tick, last error, online/offline state.
- Serialize access per port so only one request is in flight per RS485 bus.

Validation:

- With no downstream devices, timeout counters increase without deadlocking the system.
- With test device or simulated response, values update in cache.

### 5. Network Read APIs

- Add status/config/data formatting helpers.
- Expose data to W800 publish path.
- Publish heartbeat with Modbus state summary.

Validation:

- MQTT heartbeat includes port enable state and error counters.

### 6. Network Write APIs

- Add command handlers for enable mode:
  - USART2 only.
  - UART4 only.
  - both.
  - disabled.
- Add command handler for replacing the poll table.
- Validate all ranges before applying.
- Apply config atomically: invalid config leaves old config active.

Validation:

- Bad config is rejected.
- Valid config changes polling without reboot when possible.

### 7. Build, Flash, Verify

- Build with Keil command line.
- Flash with available project tooling.
- Capture available build/log evidence.
- Record final touched files and verification result.

Validation:

- Firmware builds successfully.
- Flash command succeeds if hardware/debug probe is available.
- If hardware access is unavailable, document the exact blocker and keep code build-clean.

## Risks And Decisions

- UART4 pin mapping must match the actual board. If the current `.ioc` already has UART4 generated, follow it. If not, select the board schematic/CubeMX mapping and document the chosen pins.
- W800 BLE provisioning is a later maintenance path. Wi-Fi/MQTT is the main channel for the seven checkpoints.
- Full JSON parsing is intentionally avoided in the first pass unless a local embedded JSON parser already exists.
- Configuration persistence can start as in-RAM apply plus exported config; Flash/FileX persistence is added after runtime control is stable.

