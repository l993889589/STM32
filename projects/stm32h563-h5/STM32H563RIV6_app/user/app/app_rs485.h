/**
 * @file app_rs485.h
 * @brief ThreadX dual RS-485 Modbus master service interface.
 */

#ifndef APP_RS485_H
#define APP_RS485_H

#include <stdbool.h>
#include <stdint.h>

#include "app_modbus_types.h"
#include "app_serial_stats.h"
#include "tx_api.h"

/** @brief Network payload variants exported by the RS-485 service. */
typedef enum
{
    APP_RS485_NET_STATUS = 0,
    APP_RS485_NET_CONFIG,
    APP_RS485_NET_DATA
} app_rs485_net_payload_t;

/** @brief Structured dual-port physical loopback diagnostics. */
typedef struct
{
    uint32_t server_requests;
    uint32_t server_responses;
    uint32_t master_passes;
    uint32_t master_failures;
    uint32_t server_crc_errors;
    uint32_t server_protocol_errors;
    uint16_t last_register_0;
    uint16_t last_register_1;
    uint8_t is_initialized;
} app_rs485_loopback_snapshot_t;

/** @brief Initialize RS-485 framing, synchronization, and UART callbacks. */
UINT app_rs485_init(void);

/** @brief Run the periodic dual-port Modbus master service. */
void app_rs485_task_entry(ULONG thread_input);

/** @brief Run the UART4 ld_modbus RTU server used by physical loopback tests. */
void app_rs485_server_task_entry(ULONG thread_input);

/** @brief Copy aggregated Modbus transport and protocol counters. */
void app_rs485_get_stats(app_modbus_stats_t *stats);

/** @brief Copy aggregated LDC framing counters for both RS-485 ports. */
bool app_rs485_get_ldc_stats(app_serial_stats_t *stats);

/** @brief Copy coherent master/server loopback counters and last values. */
void app_rs485_get_loopback_snapshot(app_rs485_loopback_snapshot_t *snapshot);

/** @brief Return the local Modbus unit identifier used by diagnostics. */
uint8_t app_rs485_unit_id(void);

/** @brief Format the selected RS-485 network payload as bounded JSON. */
int app_rs485_format_network_payload(app_rs485_net_payload_t type, char *out, uint16_t out_size);

/** @brief Parse and apply one bounded network configuration command. */
bool app_rs485_apply_network_command(const char *command);

#endif
