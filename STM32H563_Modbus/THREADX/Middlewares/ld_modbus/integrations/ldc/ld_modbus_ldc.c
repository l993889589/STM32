/**
 * @file ld_modbus_ldc.c
 * @brief Optional frame-in/frame-out bridge from LDC to ld_modbus RTU server.
 */

#include "ld_modbus_ldc.h"

#include <string.h>

/** @brief Bind caller-owned storage and callbacks to an LDC RTU server. */
ld_modbus_status_t ld_modbus_ldc_rtu_server_init(
    ld_modbus_ldc_rtu_server_t *server,
    ldc_easy_t *queue,
    uint8_t unit_id,
    const ld_modbus_server_map_t *map,
    ld_modbus_ldc_send_cb_t send,
    void *send_user,
    uint8_t *request_buffer,
    size_t request_capacity,
    uint8_t *response_buffer,
    size_t response_capacity)
{
    if(server == NULL || queue == NULL || map == NULL || send == NULL ||
       request_buffer == NULL || request_capacity < LD_MODBUS_RTU_MAX_ADU_LENGTH ||
       response_buffer == NULL || response_capacity < LD_MODBUS_RTU_MAX_ADU_LENGTH ||
       unit_id == LD_MODBUS_BROADCAST_UNIT_ID || unit_id > LD_MODBUS_MAX_UNIT_ID)
        return LD_MODBUS_STATUS_INVALID_ARGUMENT;

    memset(server, 0, sizeof(*server));
    server->queue = queue;
    server->map = map;
    server->send = send;
    server->send_user = send_user;
    server->unit_id = unit_id;
    server->request_buffer = request_buffer;
    server->request_capacity = request_capacity;
    server->response_buffer = response_buffer;
    server->response_capacity = response_capacity;
    return LD_MODBUS_STATUS_OK;
}

/** @brief Consume at most one LDC packet and perform one RTU server action. */
ld_modbus_status_t ld_modbus_ldc_rtu_server_poll(ld_modbus_ldc_rtu_server_t *server,
                                                 uint8_t *did_work)
{
    ld_modbus_server_action_t action;
    ld_modbus_status_t status;
    size_t response_adu_length;
    int frame_length;

    if(server == NULL || did_work == NULL || server->queue == NULL)
        return LD_MODBUS_STATUS_INVALID_ARGUMENT;
    *did_work = 0U;
    frame_length = ldc_easy_pop(server->queue,
                                server->request_buffer,
                                (uint32_t)server->request_capacity);
    if(frame_length == 0)
        return LD_MODBUS_STATUS_OK;
    if(frame_length < 0)
    {
        server->diagnostics.malformed_frames++;
        return LD_MODBUS_STATUS_BUFFER_TOO_SMALL;
    }

    *did_work = 1U;
    server->diagnostics.received_frames++;
    status = ld_modbus_server_process_rtu_adu(server->map,
                                              server->unit_id,
                                              server->request_buffer,
                                              (size_t)frame_length,
                                              server->response_buffer,
                                              server->response_capacity,
                                              &response_adu_length,
                                              &action);
    if(status != LD_MODBUS_STATUS_OK)
    {
        if(status == LD_MODBUS_STATUS_BAD_CRC)
            server->diagnostics.crc_errors++;
        else
            server->diagnostics.malformed_frames++;
        return status;
    }

    if(action == LD_MODBUS_SERVER_ACTION_IGNORED)
    {
        server->diagnostics.ignored_frames++;
        return LD_MODBUS_STATUS_OK;
    }
    if(action == LD_MODBUS_SERVER_ACTION_BROADCAST_APPLIED)
    {
        server->diagnostics.broadcast_frames++;
        return LD_MODBUS_STATUS_OK;
    }

    if(server->send(server->send_user, server->response_buffer, response_adu_length) !=
       (int)response_adu_length)
    {
        server->diagnostics.transmit_errors++;
        return LD_MODBUS_STATUS_MALFORMED_FRAME;
    }
    server->diagnostics.replied_frames++;
    return LD_MODBUS_STATUS_OK;
}

/** @brief Copy a coherent diagnostics snapshot to caller-owned storage. */
ld_modbus_status_t ld_modbus_ldc_rtu_server_get_diagnostics(
    const ld_modbus_ldc_rtu_server_t *server,
    ld_modbus_ldc_diagnostics_t *diagnostics)
{
    if(server == NULL || diagnostics == NULL)
        return LD_MODBUS_STATUS_INVALID_ARGUMENT;
    *diagnostics = server->diagnostics;
    return LD_MODBUS_STATUS_OK;
}
