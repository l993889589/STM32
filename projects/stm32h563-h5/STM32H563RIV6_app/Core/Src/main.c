/**
 * @file main.c
 * @brief STM32H563 application entry and ThreadX handoff.
 *
 * Startup is deliberately small: establish the BSP clock/timebase, initialize
 * board-owned devices, provide the visible boot indication, and enter ThreadX.
 * Peripheral handles and failure GPIO behavior remain inside user/bsp.
 */

#include "app_threadx.h"
#include "main.h"

#include "bsp.h"
#include "bsp_stop.h"

int main(void)
{
    uint32_t blink;

    if(bsp_startup() != BSP_STATUS_OK)
    {
        bsp_stop_on_error(BSP_STOP_STAGE_CLOCK);
    }
    if(bsp_init() != 0)
    {
        bsp_stop_on_error(BSP_STOP_STAGE_BSP_INIT);
    }

    for(blink = 0U; blink < 8U; blink++)
    {
        bsp_delay_ms(200U);
        bsp_led_toggle(BSP_LED_STATUS);
    }

    app_threadx_init();
    for(;;)
    {
    }
}

#ifdef USE_FULL_ASSERT
/** @brief Record an assertion as an unrecoverable runtime failure. */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    bsp_stop_on_error(BSP_STOP_STAGE_RUNTIME);
}
#endif
