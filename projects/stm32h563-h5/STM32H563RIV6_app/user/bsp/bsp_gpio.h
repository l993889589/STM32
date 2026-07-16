/**
 * @file bsp_gpio.h
 * @brief Private logical GPIO roles and safe-state interface.
 */

#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdbool.h>

#include "bsp_status.h"

/** @brief Logical board GPIO roles independent of package pins. */
typedef enum
{
    BOARD_GPIO_STATUS_LED = 0,
    BOARD_GPIO_W800_BOOT,
    BOARD_GPIO_W800_RESET,
    BOARD_GPIO_W800_WAKE,
    BOARD_GPIO_FLASH_CS,
    BOARD_GPIO_LCD_CS,
    BOARD_GPIO_LCD_DC,
    BOARD_GPIO_LCD_RESET,
    BOARD_GPIO_LCD_BACKLIGHT_SAFE,
    BOARD_GPIO_TOUCH_RESET,
    BOARD_GPIO_TOUCH_INTERRUPT,
    BOARD_GPIO_USB_ID,
    BOARD_GPIO_COUNT
} bsp_gpio_role_t;

/**
 * @brief Configure all board-owned GPIOs in documented safe states.
 * @return BSP status; repeated calls return BSP_STATUS_ALREADY_INITIALIZED.
 */
bsp_status_t bsp_gpio_init(void);

/**
 * @brief Write one logical output using its board polarity.
 * @param role Logical GPIO role.
 * @param is_active Requested logical active state.
 * @return BSP status; input-only roles return BSP_STATUS_NOT_SUPPORTED.
 */
bsp_status_t bsp_gpio_write(bsp_gpio_role_t role, bool is_active);

/**
 * @brief Read one logical GPIO using its board polarity.
 * @param role Logical GPIO role.
 * @param is_active Destination for logical state.
 * @return BSP status.
 */
bsp_status_t bsp_gpio_read(bsp_gpio_role_t role, bool *is_active);

/**
 * @brief Toggle one logical output.
 * @param role Logical GPIO role.
 * @return BSP status.
 */
bsp_status_t bsp_gpio_toggle(bsp_gpio_role_t role);

#endif /* BOARD_GPIO_H */
