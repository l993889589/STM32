/**
 * @file transport_w800_tcp.h
 * @brief Static W800 AT-socket transport for complete Modbus TCP ADUs.
 */

#ifndef TRANSPORT_W800_TCP_H
#define TRANSPORT_W800_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at_module.h"
#include "bsp_status.h"
#include "ld_modbus.h"

/** @brief W800 TCP role selected for Modbus master or slave transport. */
typedef enum
{
    TRANSPORT_W800_TCP_CLIENT = 0,
    TRANSPORT_W800_TCP_SERVER
} transport_w800_tcp_role_t;

/** @brief Caller-supplied W800 network and endpoint configuration. */
typedef struct
{
    const char *ssid;
    const char *password;
    const char *remote_host;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t uart_baud_rate;
    transport_w800_tcp_role_t role;
    uint32_t server_idle_timeout_s;
} transport_w800_tcp_config_t;

/** @brief Static runtime state for one serialized W800 TCP client socket. */
typedef struct
{
    at_session_t session;
    at_module_t module;
    int socket_id;
    int listener_socket_id;
    uint8_t receive_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint16_t receive_length;
    uint16_t expected_length;
    bool is_initialized;
    bool is_connected;
    transport_w800_tcp_role_t role;
} transport_w800_tcp_t;

/**
 * @brief Initialize USART1, join Wi-Fi, and open a client socket or listener.
 * @param transport Caller-owned static transport object.
 * @param config Caller-owned configuration; strings are consumed during this call.
 * @return BSP status.
 * @note This bounded task-context operation may wait for W800 join timeouts.
 */
bsp_status_t transport_w800_tcp_init(transport_w800_tcp_t *transport,
                                     const transport_w800_tcp_config_t *config);

/**
 * @brief Send exactly one complete Modbus TCP ADU.
 * @param transport Initialized and connected transport.
 * @param adu Complete MBAP-prefixed ADU.
 * @param length ADU length in bytes.
 * @return BSP status.
 */
bsp_status_t transport_w800_tcp_send_adu(transport_w800_tcp_t *transport,
                                         const uint8_t *adu,
                                         size_t length);

/**
 * @brief Poll the W800 stream until one complete MBAP-framed ADU is available.
 * @param transport Initialized and connected transport.
 * @param adu Caller-owned output buffer.
 * @param capacity Output capacity.
 * @param length Receives a complete ADU length, or zero when no ADU is ready.
 * @return OK for a complete ADU, NOT_READY for no complete ADU, or an error.
 */
bsp_status_t transport_w800_tcp_try_read_adu(transport_w800_tcp_t *transport,
                                             uint8_t *adu,
                                             size_t capacity,
                                             size_t *length);

/**
 * @brief Close the active W800 socket and server listener when applicable.
 * @param transport Initialized transport.
 * @return BSP status.
 */
bsp_status_t transport_w800_tcp_close(transport_w800_tcp_t *transport);

#endif
