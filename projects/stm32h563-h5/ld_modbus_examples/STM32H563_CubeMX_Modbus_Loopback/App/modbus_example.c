/**
 * @file modbus_example.c
 * @brief Compile-time routing between the minimal slave and loopback test.
 */

#include "modbus_example.h"

#include "modbus_example_config.h"

#if MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_SLAVE
#include "modbus_slave_example.h"
#else
#include "modbus_loopback.h"
#endif

/** @brief Initialize the compile-time selected example implementation. */
HAL_StatusTypeDef modbus_example_init(void)
{
#if MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_SLAVE
    return modbus_slave_example_init();
#else
    return modbus_loopback_init();
#endif
}

/** @brief Poll the compile-time selected example implementation. */
void modbus_example_poll(void)
{
#if MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_SLAVE
    modbus_slave_example_poll();
#else
    modbus_loopback_poll();
#endif
}
