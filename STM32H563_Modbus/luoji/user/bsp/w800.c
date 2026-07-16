/**
 * @file w800.c
 * @brief W800 module GPIO sequencing implementation.
 */

#include "w800.h"

#include <stddef.h>

#include "board_control.h"
#include "bsp_time.h"

static w800_config_t w800_config;
static bool w800_is_initialized;

/**
 * @brief Execute the configured active-low reset pulse and settle delay.
 */
static bsp_status_t w800_reset_sequence(void)
{
    bsp_status_t status;

    status = board_control_write(BOARD_CONTROL_WIFI_RESET, true);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    bsp_time_delay_ms(w800_config.reset_hold_ms);

    status = board_control_write(BOARD_CONTROL_WIFI_RESET, false);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    bsp_time_delay_ms(w800_config.boot_settle_ms);
    return BSP_STATUS_OK;
}

/**
 * @brief Implement w800_init() as documented by its interface contract.
 */
bsp_status_t w800_init(const w800_config_t *config)
{
    bsp_status_t status;

    if((config == NULL) || (config->reset_hold_ms == 0U) ||
       (config->boot_settle_ms == 0U) ||
       (config->boot_mode > W800_BOOT_DOWNLOAD))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(w800_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    w800_config = *config;
    status = board_control_write(BOARD_CONTROL_WIFI_WAKE, false);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = board_control_write(BOARD_CONTROL_WIFI_BOOT,
                                 config->boot_mode == W800_BOOT_NORMAL);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = w800_reset_sequence();
    if(status == BSP_STATUS_OK)
    {
        w800_is_initialized = true;
    }
    return status;
}

/**
 * @brief Implement w800_set_wake() as documented by its interface contract.
 */
bsp_status_t w800_set_wake(bool is_requested)
{
    return w800_is_initialized ?
           board_control_write(BOARD_CONTROL_WIFI_WAKE, is_requested) :
           BSP_STATUS_NOT_READY;
}

/**
 * @brief Implement w800_reset() as documented by its interface contract.
 */
bsp_status_t w800_reset(void)
{
    return w800_is_initialized ? w800_reset_sequence() :
           BSP_STATUS_NOT_READY;
}
