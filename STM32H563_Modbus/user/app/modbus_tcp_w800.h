/**
 * @file modbus_tcp_w800.h
 * @brief Static Modbus TCP client and server services over the W800 transport.
 */

#ifndef MODBUS_TCP_W800_H
#define MODBUS_TCP_W800_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ld_modbus_server.h"
#include "transport_w800_tcp.h"

/** @brief Static state for one Modbus TCP master/client connection. */
typedef struct
{
    transport_w800_tcp_t transport;
    uint8_t request_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint8_t response_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint16_t next_transaction_id;
    uint16_t pending_transaction_id;
    uint8_t unit_id;
    bool is_pending;
} modbus_tcp_w800_client_t;

/** @brief Static state for one Modbus TCP slave/server listener. */
typedef struct
{
    transport_w800_tcp_t transport;
    const ld_modbus_server_map_t *map;
    uint8_t request_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint8_t response_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
} modbus_tcp_w800_server_t;

/** @brief Connect a W800-backed Modbus TCP master to one remote endpoint. */
bsp_status_t modbus_tcp_w800_client_init(
    modbus_tcp_w800_client_t *client,
    const transport_w800_tcp_config_t *config,
    uint8_t unit_id);

/** @brief Encode and send one caller-built Modbus request PDU. */
bsp_status_t modbus_tcp_w800_client_begin(modbus_tcp_w800_client_t *client,
                                          const uint8_t *request_pdu,
                                          size_t request_pdu_length);

/**
 * @brief Poll for the pending response and return a zero-copy validated view.
 * @note The view remains valid until the next client poll or begin operation.
 */
bsp_status_t modbus_tcp_w800_client_poll(modbus_tcp_w800_client_t *client,
                                         ld_modbus_adu_view_t *response);

/** @brief Join Wi-Fi and create a W800-backed Modbus TCP slave listener. */
bsp_status_t modbus_tcp_w800_server_init(
    modbus_tcp_w800_server_t *server,
    const transport_w800_tcp_config_t *config,
    const ld_modbus_server_map_t *map);

/** @brief Process at most one complete TCP request and send its response. */
bsp_status_t modbus_tcp_w800_server_step(modbus_tcp_w800_server_t *server,
                                         bool *did_work);

#endif
