/**
 * @file app.c
 * @brief Compile-time routing between the RTU slave and master examples.
 */

#include "app.h"

#include "modbus_app_config.h"

#if MODBUS_APP_ROLE == MODBUS_APP_ROLE_SLAVE
#include "modbus_slave_example.h"
#else
#include "modbus_master_example.h"
#endif

/** @brief Initialize the configured bare-metal Modbus example. */
HAL_StatusTypeDef app_init(void)
{
#if MODBUS_APP_ROLE == MODBUS_APP_ROLE_SLAVE
    return modbus_slave_example_init();
#else
    return modbus_master_example_init();
#endif
}

/** @brief Poll the configured bare-metal Modbus example. */
void app_poll(void)
{
#if MODBUS_APP_ROLE == MODBUS_APP_ROLE_SLAVE
    modbus_slave_example_poll();
#else
    modbus_master_example_poll();
#endif
}
