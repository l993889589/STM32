/**
 * @file modbus_example.h
 * @brief Common lifecycle used by either selectable CubeMX Modbus example.
 */

#ifndef MODBUS_EXAMPLE_H
#define MODBUS_EXAMPLE_H

#include "stm32h5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the example selected in modbus_example_config.h. */
HAL_StatusTypeDef modbus_example_init(void);

/** @brief Run one bounded poll step for the selected example. */
void modbus_example_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_EXAMPLE_H */
