/**
 * @file bsp_board.c
 * @brief Board-level initialization for SPI buses, flash, and backlight PWM.
 */

#include "bsp_board.h"

#include <stdint.h>

#include "board_resources.h"
#include "bsp_pwm.h"
#include "bsp_spi.h"
#include "gd25lq128.h"

/** @brief Initialize non-UART board resources without CubeMX peripheral handles. */
int bsp_board_init(void)
{
    const bsp_spi_config_t flash_spi =
    {
        .baud_rate_hz = BOARD_SPI_FLASH_MAX_CLOCK_HZ,
        .clock_polarity = BSP_SPI_CLOCK_POLARITY_LOW,
        .clock_phase = BSP_SPI_CLOCK_PHASE_FIRST_EDGE
    };
    const bsp_spi_config_t display_spi =
    {
        .baud_rate_hz = BOARD_SPI_LCD_MAX_CLOCK_HZ,
        .clock_polarity = BSP_SPI_CLOCK_POLARITY_LOW,
        .clock_phase = BSP_SPI_CLOCK_PHASE_FIRST_EDGE
    };
    const bsp_pwm_config_t backlight_pwm =
    {
        .frequency_hz = BOARD_PWM_LCD_DEFAULT_FREQUENCY_HZ,
        .duty_permille = 0U
    };
    bsp_status_t status;

    status = bsp_spi_init(BOARD_SPI_FLASH, &flash_spi);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }
    status = bsp_spi_init(BOARD_SPI_DISPLAY, &display_spi);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }
    status = bsp_pwm_init(BOARD_PWM_LCD_BACKLIGHT, &backlight_pwm, NULL);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }
    if((bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT) != BSP_STATUS_OK) ||
       !gd25lq128_init())
    {
        return -1;
    }
    return 0;
}
