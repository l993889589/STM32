/**
 * @file bsp.c
 * @brief Ordered board-support initialization and compatibility services.
 */

#include "bsp.h"

#include <stdio.h>

#include "bsp_cache.h"
#include "bsp_clock.h"
#include "bsp_config.h"
#include "bsp_gpio.h"
#include "bsp_spi.h"
#include "bsp_stop.h"
#include "tx_api.h"

/** @brief Initialize the board-owned buses and devices. */
static int bsp_peripherals_init(void);

/** @brief Board clock values validated after system-clock configuration. */
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

/** @brief Establish HAL timebase and the board clock before device init. */
bsp_status_t bsp_startup(void)
{
    if(HAL_Init() != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    return bsp_clock_configure_system();
}

/** @brief Initialize HAL and the board clock using the H7 demo entry shape. */
void system_init(void)
{
    if(bsp_startup() != BSP_STATUS_OK)
    {
        BSP_ERROR();
    }
}

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

/** @brief Delay without busy-waiting when called from a runnable ThreadX task. */
void bsp_delay_ms(uint32_t delay_ms)
{
    uint64_t ticks;

    if(delay_ms == 0U)
    {
        return;
    }

    if((__get_IPSR() == 0U) && (__get_PRIMASK() == 0U) &&
       (__get_BASEPRI() == 0U) && (__get_FAULTMASK() == 0U) &&
       (tx_thread_identify() != TX_NULL))
    {
        ticks = ((uint64_t)delay_ms * TX_TIMER_TICKS_PER_SECOND + 999ULL) /
                1000ULL;
        if(ticks == 0ULL)
        {
            ticks = 1ULL;
        }
        while(ticks > (uint64_t)(TX_WAIT_FOREVER - 1UL))
        {
            (void)tx_thread_sleep(TX_WAIT_FOREVER - 1UL);
            ticks -= (uint64_t)(TX_WAIT_FOREVER - 1UL);
        }
        (void)tx_thread_sleep((ULONG)ticks);
        return;
    }

    bsp_dwt_delay_ms(delay_ms);
}

/** @brief Busy-wait for a short microsecond interval through the DWT owner. */
void bsp_delay_us(uint32_t delay_us)
{
    bsp_dwt_delay_us(delay_us);
}

/** @brief Route an unrecoverable BSP failure to the board-visible stop state. */
void bsp_error_handler(const char *file, uint32_t line)
{
    (void)file;
    (void)line;
    bsp_stop_on_error(BSP_STOP_STAGE_RUNTIME);
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
