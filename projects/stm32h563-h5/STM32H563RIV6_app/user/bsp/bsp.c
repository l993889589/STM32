/**
 * @file bsp.c
 * @brief Ordered board-support initialization and compatibility services.
 */

#include "bsp.h"

#include <stdio.h>

#include "bsp_gpio.h"
#include "bsp_config.h"
#include "bsp_cache.h"
#include "bsp_clock.h"
#include "bsp_spi.h"

/** @brief Initialize the board-owned buses and devices. */
static int bsp_peripherals_init(void);

static const bsp_clock_config_t g_bsp_clock_config =
{
    .hse_frequency_hz = BOARD_HSE_FREQUENCY_HZ,
    .pll1_m = 2U,
    .pll1_n = 40U,
    .pll1_p = 2U,
    .pll1_q = 2U,
    .pll1_r = 2U,
    .expected_sysclk_hz = BOARD_EXPECTED_SYSCLK_HZ
};

/** @brief Implement the application-facing system reset boundary. */
void bsp_system_reset(void)
{
    NVIC_SystemReset();
}

/** @brief Initialize required BSP resources in safe dependency order. */
int bsp_init(void)
{
    static uint8_t initialized;
    bsp_status_t status;

    if(initialized != 0U)
    {
        return 0;
    }

    status = bsp_clock_init(&g_bsp_clock_config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }

    status = bsp_gpio_init();
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }
    status = bsp_cache_init();
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }

    if((bsp_peripherals_init() != 0) || (bsp_uart_init() != 0))
    {
        return -1;
    }

    (void)bsp_dwt_init();
    bsp_timer_init();
    bsp_w800_reset_release();
    bsp_led_off(BSP_LED_STATUS);
    bsp_led_init();
    (void)bsp_lcd_init();
    (void)bsp_touch_init();

    initialized = 1U;
    return 0;
}

/** @brief Turn on one logical board LED through the board GPIO owner. */
void bsp_led_on(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
    {
        (void)bsp_gpio_write(BOARD_GPIO_STATUS_LED, true);
    }
}

/** @brief Turn off one logical board LED through the board GPIO owner. */
void bsp_led_off(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
    {
        (void)bsp_gpio_write(BOARD_GPIO_STATUS_LED, false);
    }
}

/** @brief Toggle one logical board LED through the board GPIO owner. */
void bsp_led_toggle(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
    {
        (void)bsp_gpio_toggle(BOARD_GPIO_STATUS_LED);
    }
}

/** @brief Assert the W800 hardware reset output. */
void bsp_w800_reset_assert(void)
{
    (void)bsp_gpio_write(BOARD_GPIO_W800_RESET, true);
}

/** @brief Release the W800 hardware reset output. */
void bsp_w800_reset_release(void)
{
    (void)bsp_gpio_write(BOARD_GPIO_W800_RESET, false);
}

/** @brief Perform a bounded W800 hardware reset sequence. */
void bsp_w800_hard_reset(uint32_t assert_ms, uint32_t ready_ms)
{
    bsp_w800_reset_assert();
    HAL_Delay(assert_ms);
    bsp_w800_reset_release();
    HAL_Delay(ready_ms);
}

/** @brief Read the SPI NOR identity and emit one formatted line. */
void bsp_spi_nor_log_id(void (*write_line)(const char *line))
{
    bsp_flash_id_t id;
    char line[96];

    if(write_line == NULL)
    {
        return;
    }

    if(bsp_flash_read_id(&id))
    {
        (void)snprintf(line,
                       sizeof(line),
                       "spi nor jedec: %02X %02X %02X\r\n",
                       id.manufacturer_id,
                       id.memory_type,
                       id.capacity);
        write_line(line);
    }
    else
    {
        write_line("spi nor jedec: read failed\r\n");
    }
}

/* Board peripheral composition. */


#include <stdint.h>






/** @brief Initialize non-UART board resources without CubeMX peripheral handles. */
static int bsp_peripherals_init(void)
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
       !bsp_flash_init())
    {
        return -1;
    }
    return 0;
}
