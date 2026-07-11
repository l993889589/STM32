/**
 * @file ld_modbus_ldc.h
 * @brief Optional LDC adapter for a bounded Modbus RTU server poll loop.
 */

#ifndef LD_MODBUS_LDC_H
#define LD_MODBUS_LDC_H

#include "ld_modbus_server.h"
#include "ldc_easy.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Transport callback used to send one complete RTU response ADU. */
typedef int (*ld_modbus_ldc_send_cb_t)(void *user,
                                       const uint8_t *data,
                                       size_t length);

/** @brief Monotonic diagnostics maintained by the LDC adapter. */
typedef struct
{
    uint32_t received_frames;
    uint32_t replied_frames;
    uint32_t broadcast_frames;
    uint32_t ignored_frames;
    uint32_t crc_errors;
    uint32_t malformed_frames;
    uint32_t transmit_errors;
} ld_modbus_ldc_diagnostics_t;

/** @brief Caller-owned state for one statically allocated LDC RTU server. */
typedef struct
{
    ldc_easy_t *queue;
    const ld_modbus_server_map_t *map;
    ld_modbus_ldc_send_cb_t send;
    void *send_user;
    uint8_t unit_id;
    uint8_t *request_buffer;
    size_t request_capacity;
    uint8_t *response_buffer;
    size_t response_capacity;
    ld_modbus_ldc_diagnostics_t diagnostics;
} ld_modbus_ldc_rtu_server_t;

/**
 * @brief Bind an application-owned LDC queue, Modbus map, and ADU buffers.
 * @note The context and every referenced object must outlive all poll calls.
 */
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
    size_t response_capacity);

/**
 * @brief Process at most one complete LDC packet.
 * @param did_work Set to one when a queued packet was consumed, otherwise zero.
 * @note Call from one task or superloop owner; never call from an ISR.
 */
ld_modbus_status_t ld_modbus_ldc_rtu_server_poll(ld_modbus_ldc_rtu_server_t *server,
                                                 uint8_t *did_work);

/** @brief Copy current adapter diagnostics into caller-owned storage. */
ld_modbus_status_t ld_modbus_ldc_rtu_server_get_diagnostics(
    const ld_modbus_ldc_rtu_server_t *server,
    ld_modbus_ldc_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
