/**
 * @file bsp_led.h
 * @brief Logical CHPM status LED interface.
 */

#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

typedef enum
{
    BSP_LED_STATUS = 0,
    BSP_LED_COUNT
} bsp_led_t;

/** @brief Configure every LED in its inactive safe state. */
void bsp_led_init(void);

/** @brief Turn on one logical LED. */
void bsp_led_on(bsp_led_t led);

/** @brief Turn off one logical LED. */
void bsp_led_off(bsp_led_t led);

/** @brief Toggle one logical LED. */
void bsp_led_toggle(bsp_led_t led);

/** @brief Return one when the selected LED is lit. */
uint8_t bsp_led_is_on(bsp_led_t led);

#endif /* BSP_LED_H */
