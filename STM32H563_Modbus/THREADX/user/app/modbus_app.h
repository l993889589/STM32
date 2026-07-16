/**
 * @file modbus_app.h
 * @brief Board application service for the RS485-1 Modbus RTU slave.
 */

#ifndef MODBUS_APP_H
#define MODBUS_APP_H

#include <stdint.h>

#include "bsp_status.h"

/**
 * @brief Initialize USART2, LDC framing, and the static Modbus register map.
 * @return BSP status describing initialization success or the first failure.
 */
bsp_status_t modbus_app_init(void);

/**
 * @brief Advance receive framing and process a bounded number of RTU requests.
 * @param elapsed_us Microseconds elapsed since the previous call.
 * @return BSP status; malformed bus traffic is counted and does not stop service.
 */
bsp_status_t modbus_app_step(uint32_t elapsed_us);

#endif
