#include "app_services.h"

#include <stdbool.h>

#include "board.h"
#include "bsp_soft_timer.h"
#include "drv_dwin.h"
#include "drv_modbus_port.h"
#include "dwin_ldc_channel.h"
#include "app_rx_handlers.h"

static app_services_tick_hook_t tick_hook;
static bool services_initialized;

bsp_status_t app_services_init(const app_services_config_t *config)
{
    bsp_status_t status;

    if(config == NULL || config->modbus_baud_rate == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(services_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    if(!dwin_ldc_channel_init(dwin_uart_queue_handler))
        return BSP_STATUS_IO_ERROR;

    status = drv_modbus_port_init(config->modbus_baud_rate);
    if(status != BSP_STATUS_OK)
        return status;
    status = drv_dwin_init();
    if(status != BSP_STATUS_OK)
        return status;
    status = board_start_io();
    if(status != BSP_STATUS_OK)
        return status;

    tick_hook = config->tick_1ms_hook;
    services_initialized = true;
    return BSP_STATUS_OK;
}

void app_services_tick_1ms(void)
{
    if(!services_initialized)
        return;
    dwin_ldc_channel_tick(1U);
    if(tick_hook != NULL)
        tick_hook();
    SysTick_ISR();
}
