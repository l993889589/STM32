/**
 * @file bsp.c
 * @brief Ordered composition of independently owned STM32F401 BSP modules.
 */

#include "bsp.h"

#include "bsp_clock.h"
#include "bsp_control.h"
#include "bsp_dwt.h"
#include "bsp_i2c.h"
#include "bsp_led.h"
#include "bsp_sensor.h"
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "stm32f4xx_hal.h"

static bsp_init_stage_t current_stage;

/** @brief Initialize HAL and establish the 84 MHz board clock tree. */
bsp_status_t bsp_startup(void)
{
    if(HAL_Init() != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    return bsp_clock_configure_system();
}

/** @brief Initialize each required board owner in deterministic order. */
bsp_status_t bsp_init(void)
{
    bsp_status_t status;

    if(current_stage == BSP_INIT_STAGE_READY)
        return BSP_STATUS_ALREADY_INITIALIZED;

    current_stage = BSP_INIT_STAGE_CLOCK;
    status = bsp_clock_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BSP_INIT_STAGE_DWT;
    status = bsp_dwt_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BSP_INIT_STAGE_SAFE_GPIO;
    bsp_led_init();
    bsp_led_off(BSP_LED_STATUS);
    status = bsp_control_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BSP_INIT_STAGE_BUSES;
    status = bsp_spi_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;
    status = bsp_i2c_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;
    status = bsp_sensor_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BSP_INIT_STAGE_UART;
    status = bsp_uart_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BSP_INIT_STAGE_READY;
    return BSP_STATUS_OK;
}

/** @brief Start UART receive paths after all owners have registered callbacks. */
bsp_status_t bsp_start_io(void)
{
    if(current_stage != BSP_INIT_STAGE_READY)
        return BSP_STATUS_NOT_READY;
    return bsp_uart_start_rx();
}

/** @brief Return the most recently reached BSP initialization stage. */
bsp_init_stage_t bsp_init_stage(void)
{
    return current_stage;
}
