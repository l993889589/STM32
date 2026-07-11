/**
 * @file board_config.h
 * @brief Authoritative STM32H563 board resource and electrical binding.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "bsp_clock.h"

#define BOARD_NAME                       "dshan_h563_industrial"
#define BOARD_HSE_FREQUENCY_HZ           (25000000UL)
#define BOARD_EXPECTED_SYSCLK_HZ         (250000000UL)
#define BOARD_STATUS_LED_ACTIVE_LOW      (1U)
#define BOARD_LCD_BACKLIGHT_SAFE_LEVEL   (0U)

extern const bsp_clock_config_t board_clock_config;

#endif
