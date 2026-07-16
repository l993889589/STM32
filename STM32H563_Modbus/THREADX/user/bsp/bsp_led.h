/**
 * @file bsp_led.h
 * @brief Logical board LED interface.
 */

#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdbool.h>

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOARD_LED_STATUS = 0,
    BOARD_LED_COUNT
} board_led_role_t;

/**
 * Initialize a logical board LED in its inactive state.
 * @param role Logical LED role.
 * @return BSP status; repeated initialization is reported explicitly.
 */
bsp_status_t bsp_led_init(board_led_role_t role);
/**
 * Set a logical board LED state while applying board polarity.
 * @param role Logical LED role.
 * @param is_on True to illuminate the LED; false to turn it off.
 * @return BSP status.
 */
bsp_status_t bsp_led_set(board_led_role_t role, bool is_on);
/**
 * Toggle a previously initialized logical board LED.
 * @param role Logical LED role.
 * @return BSP status.
 */
bsp_status_t bsp_led_toggle(board_led_role_t role);

#ifdef __cplusplus
}
#endif

#endif
