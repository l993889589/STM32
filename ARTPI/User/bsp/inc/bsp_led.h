#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdint.h>

typedef enum
{
    BSP_LED_BLUE = 0,
    BSP_LED_RED,
    BSP_LED_COUNT
} bsp_led_t;

void bsp_led_init(void);
void bsp_led_on(bsp_led_t led);
void bsp_led_off(bsp_led_t led);
void bsp_led_toggle(bsp_led_t led);
uint8_t bsp_led_is_on(bsp_led_t led);

#endif

