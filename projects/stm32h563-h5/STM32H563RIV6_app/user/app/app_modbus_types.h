/**
 * @file app_modbus_types.h
 * @brief Application-facing Modbus diagnostics independent of protocol engines.
 */

#ifndef APP_MODBUS_TYPES_H
#define APP_MODBUS_TYPES_H

#include <stdint.h>

/** @brief Runtime counters exported by the dual RS-485 Modbus service. */
typedef struct
{
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t ignored_frames;
    uint32_t crc_errors;
    uint32_t exceptions;
    uint32_t transport_errors;
} app_modbus_stats_t;

#endif /* APP_MODBUS_TYPES_H */
