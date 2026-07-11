/**
 * @file modbus_tcp_w800.c
 * @brief W800 Modbus TCP master/client and slave/server bounded services.
 */

#include "modbus_tcp_w800.h"

#include <string.h>

/** @brief Initialize one static W800 Modbus TCP client context. */
bsp_status_t modbus_tcp_w800_client_init(
    modbus_tcp_w800_client_t *client,
    const transport_w800_tcp_config_t *config,
    uint8_t unit_id)
{
    if((client == NULL) || (config == NULL) ||
       (config->role != TRANSPORT_W800_TCP_CLIENT) ||
       (unit_id > LD_MODBUS_MAX_UNIT_ID))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    memset(client, 0, sizeof(*client));
    client->unit_id = unit_id;
    client->next_transaction_id = 1U;
    return transport_w800_tcp_init(&client->transport, config);
}

/** @brief Begin one transaction using a caller-built PDU. */
bsp_status_t modbus_tcp_w800_client_begin(modbus_tcp_w800_client_t *client,
                                          const uint8_t *request_pdu,
                                          size_t request_pdu_length)
{
    ld_modbus_status_t protocol_status;
    size_t request_length;
    bsp_status_t status;

    if((client == NULL) || (request_pdu == NULL) ||
       (request_pdu_length == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(client->is_pending)
    {
        return BSP_STATUS_BUSY;
    }

    client->pending_transaction_id = client->next_transaction_id++;
    protocol_status = ld_modbus_tcp_encode(client->pending_transaction_id,
                                           client->unit_id,
                                           request_pdu,
                                           request_pdu_length,
                                           client->request_adu,
                                           sizeof(client->request_adu),
                                           &request_length);
    if(protocol_status != LD_MODBUS_STATUS_OK)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = transport_w800_tcp_send_adu(&client->transport,
                                         client->request_adu, request_length);
    if(status == BSP_STATUS_OK)
    {
        client->is_pending = true;
    }
    return status;
}

/** @brief Poll and validate transaction/unit identifiers for one response. */
bsp_status_t modbus_tcp_w800_client_poll(modbus_tcp_w800_client_t *client,
                                         ld_modbus_adu_view_t *response)
{
    ld_modbus_adu_view_t view;
    ld_modbus_status_t protocol_status;
    size_t response_length;
    bsp_status_t status;

    if((client == NULL) || (response == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!client->is_pending)
    {
        return BSP_STATUS_NOT_READY;
    }
    status = transport_w800_tcp_try_read_adu(&client->transport,
                                             client->response_adu,
                                             sizeof(client->response_adu),
                                             &response_length);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    protocol_status = ld_modbus_tcp_decode(client->response_adu,
                                           response_length, &view);
    if((protocol_status != LD_MODBUS_STATUS_OK) ||
       (view.transaction_id != client->pending_transaction_id) ||
       (view.unit_id != client->unit_id))
    {
        client->is_pending = false;
        return BSP_STATUS_IO_ERROR;
    }
    *response = view;
    client->is_pending = false;
    return BSP_STATUS_OK;
}

/** @brief Initialize one static W800 Modbus TCP server context. */
bsp_status_t modbus_tcp_w800_server_init(
    modbus_tcp_w800_server_t *server,
    const transport_w800_tcp_config_t *config,
    const ld_modbus_server_map_t *map)
{
    if((server == NULL) || (config == NULL) || (map == NULL) ||
       (config->role != TRANSPORT_W800_TCP_SERVER))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    memset(server, 0, sizeof(*server));
    server->map = map;
    return transport_w800_tcp_init(&server->transport, config);
}

/** @brief Receive, process, and respond to at most one complete TCP ADU. */
bsp_status_t modbus_tcp_w800_server_step(modbus_tcp_w800_server_t *server,
                                         bool *did_work)
{
    ld_modbus_status_t protocol_status;
    size_t request_length;
    size_t response_length;
    bsp_status_t status;

    if((server == NULL) || (did_work == NULL) || (server->map == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *did_work = false;
    status = transport_w800_tcp_try_read_adu(&server->transport,
                                             server->request_adu,
                                             sizeof(server->request_adu),
                                             &request_length);
    if(status == BSP_STATUS_NOT_READY)
    {
        return BSP_STATUS_OK;
    }
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    protocol_status = ld_modbus_server_process_tcp_adu(
        server->map, server->request_adu, request_length,
        server->response_adu, sizeof(server->response_adu),
        &response_length);
    if(protocol_status != LD_MODBUS_STATUS_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    status = transport_w800_tcp_send_adu(&server->transport,
                                         server->response_adu,
                                         response_length);
    if(status == BSP_STATUS_OK)
    {
        *did_work = true;
    }
    return status;
}
