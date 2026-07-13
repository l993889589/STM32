/**
 * @file app.h
 * @brief Shared ld_modbus example application entry points.
 */

#ifndef APP_H
#define APP_H

#include "stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the selected Modbus role after CubeMX peripheral setup. */
HAL_StatusTypeDef app_init(void);

/** @brief Run one bounded iteration of the selected Modbus role. */
void app_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
