/**
 * @file bsp_led.c
 * @brief Legacy numeric LED wrappers for the logical board GPIO owner.
 */

#include "bsp_led.h"

#include <stdbool.h>

#include "bsp_gpio.h"
#include "bsp.h"

/** @brief Validate one legacy numeric LED index. */
static bool bsp_led_index_is_valid(BSP_LED_Index_t led)
{
    return led < LED_COUNT;
}

/** @brief Ensure board GPIO initialization is complete. */
void bsp_led_init(void)
{
    bsp_status_t status = bsp_gpio_init();
    (void)status;
}

/** @brief Turn on one legacy numeric LED. */
void bsp_ledn_on(BSP_LED_Index_t led)
{
    if(bsp_led_index_is_valid(led))
    {
        bsp_led_on(BSP_LED_STATUS);
    }
}

/** @brief Turn off one legacy numeric LED. */
void bsp_ledn_off(BSP_LED_Index_t led)
{
    if(bsp_led_index_is_valid(led))
    {
        bsp_led_off(BSP_LED_STATUS);
    }
}

/** @brief Toggle one legacy numeric LED. */
void bsp_ledn_toggle(BSP_LED_Index_t led)
{
    if(bsp_led_index_is_valid(led))
    {
        bsp_led_toggle(BSP_LED_STATUS);
    }
}

/** @brief Turn on every configured LED. */
void bsp_ledn_allon(void)
{
    bsp_ledn_on(LED0);
}

/** @brief Turn off every configured LED. */
void bsp_ledn_alloff(void)
{
    bsp_ledn_off(LED0);
}

/** @brief Read one legacy numeric LED state. */
uint8_t bsp_ledn_getstate(BSP_LED_Index_t led)
{
    bool is_on = false;

    if(!bsp_led_index_is_valid(led) ||
       (bsp_gpio_read(BOARD_GPIO_STATUS_LED, &is_on) != BSP_STATUS_OK))
    {
        return 0U;
    }
    return is_on ? 1U : 0U;
}
