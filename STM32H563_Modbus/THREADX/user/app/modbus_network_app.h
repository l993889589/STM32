/**
 * @file modbus_network_app.h
 * @brief Optional bounded W800 Modbus TCP client/server application service.
 */

#ifndef MODBUS_NETWORK_APP_H
#define MODBUS_NETWORK_APP_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Initialize the selected W800 role, or return OK when disabled. */
bsp_status_t modbus_network_app_init(void);

/**
 * @brief Advance at most one W800 TCP operation from task/superloop context.
 * @param elapsed_ms Milliseconds since the previous call.
 */
bsp_status_t modbus_network_app_step(uint32_t elapsed_ms);

/** @brief Close the active socket and return the service to its initial state. */
bsp_status_t modbus_network_app_deinit(void);

#endif
