/**
 * @file freertos_app.h
 * @brief Static FreeRTOS task startup for the ld_modbus example.
 */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

#include "stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the static Modbus task and start the FreeRTOS scheduler.
 * @return HAL_ERROR only if task creation fails or the scheduler returns.
 */
HAL_StatusTypeDef freertos_app_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_APP_H */
