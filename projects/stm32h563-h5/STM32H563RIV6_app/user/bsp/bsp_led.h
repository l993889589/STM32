/**
 * @file bsp_led.h
 * @brief Legacy numeric LED wrappers backed by the logical board LED.
 */

#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

/** @brief Numeric LED index retained for existing application calls. */
typedef enum
{
    LED0 = 0,
    LED_COUNT
} BSP_LED_Index_t;

/** @brief Ensure the logical status LED is initialized. */
void bsp_led_init(void);
/** @brief Turn on a numeric LED. @param led Numeric LED index. */
void bsp_ledn_on(BSP_LED_Index_t led);
/** @brief Turn off a numeric LED. @param led Numeric LED index. */
void bsp_ledn_off(BSP_LED_Index_t led);
/** @brief Toggle a numeric LED. @param led Numeric LED index. */
void bsp_ledn_toggle(BSP_LED_Index_t led);
/** @brief Turn on all configured numeric LEDs. */
void bsp_ledn_allon(void);
/** @brief Turn off all configured numeric LEDs. */
void bsp_ledn_alloff(void);
/**
 * @brief Read a numeric LED logical state.
 * @param led Numeric LED index.
 * @return One when lit, otherwise zero.
 */
uint8_t bsp_ledn_getstate(BSP_LED_Index_t led);

#endif /* BSP_LED_H */
