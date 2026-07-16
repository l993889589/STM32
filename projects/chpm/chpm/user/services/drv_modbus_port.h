/**
 * @file drv_modbus_port.h
 * @brief Thread-owned Modbus RTU port over strict timestamp framing.
 */

#ifndef DRV_MODBUS_PORT_H
#define DRV_MODBUS_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"
#include "ld_modbus_rtu_framer.h"

/** @brief Monotonic integration diagnostics outside the protocol framer. */
typedef struct
{
    uint32_t rx_chunks;
    uint32_t rx_bytes;
    uint32_t invalid_rx_metadata;
    uint32_t uart_resets;
} drv_modbus_port_diagnostics_t;

/** @brief Initialize the UART callback, strict RTU framer, and ThreadX event. */
bsp_status_t drv_modbus_port_init(uint32_t baud_rate);

/**
 * @brief Wait for one complete strict T1.5/T3.5 RTU frame.
 * @return Positive frame length, zero on timeout, or -1 on invalid/buffer error.
 */
int drv_modbus_port_read_frame(uint8_t *data,
                               uint16_t capacity,
                               uint32_t timeout_ms);

/** @brief Transmit one complete RTU response with a bounded UART wait. */
bsp_status_t drv_modbus_port_write(const uint8_t *data,
                                   uint16_t length,
                                   uint32_t timeout_ms);

/** @brief Copy coherent strict-framer and port diagnostic snapshots. */
bool drv_modbus_port_get_diagnostics(
    ld_modbus_rtu_framer_diag_t *framer,
    drv_modbus_port_diagnostics_t *port);

#endif /* DRV_MODBUS_PORT_H */
