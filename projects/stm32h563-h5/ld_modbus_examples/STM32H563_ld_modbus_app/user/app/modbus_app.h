/**
 * @file modbus_app.h
 * @brief App-only Modbus RTU slave service for the STM32H563 example.
 */

#ifndef MODBUS_APP_H
#define MODBUS_APP_H

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize UART, static register maps, and RTU timing state. */
bsp_status_t modbus_app_init(void);
/** @brief Run bounded Modbus receive and response work from the superloop. */
void modbus_app_poll(void);

#ifdef __cplusplus
}
#endif

#endif
