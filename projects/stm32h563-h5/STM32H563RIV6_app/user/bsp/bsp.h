/**
 * @file bsp.h
 * @brief Top-level board-support initialization and compatibility services.
 */

#ifndef BSP_H
#define BSP_H

#include <stdint.h>

#include "bsp_dwt.h"
#include "bsp_lcd.h"
#include "bsp_led.h"
#include "bsp_platform.h"
#include "bsp_pwm.h"
#include "bsp_timer.h"
#include "bsp_touch.h"
#include "bsp_uart.h"
#include "bsp_flash.h"

#define BSP_VERSION ("0.3.0")

/** @brief Stop at the common BSP fatal-error boundary. */
#define BSP_ERROR() bsp_error_handler(__FILE__, __LINE__)

/** @brief Logical status LED role retained for application compatibility. */
typedef enum
{
    BSP_LED_STATUS = 0
} bsp_led_t;

/** @brief Initialize HAL and configure the board system clock. */
bsp_status_t bsp_startup(void);

/**
 * @brief H7-demo-compatible system startup wrapper.
 * @note Calls BSP_ERROR() when HAL or clock initialization fails.
 */
void system_init(void);

/** @brief Initialize all required BSP resources once. @return Zero on success. */
int bsp_init(void);

/**
 * @brief Delay in milliseconds using ThreadX when a task context is available.
 * @param delay_ms Requested delay in milliseconds.
 */
void bsp_delay_ms(uint32_t delay_ms);

/** @brief Busy-wait for a short microsecond interval. */
void bsp_delay_us(uint32_t delay_us);

/**
 * @brief Enter the board fatal-error state.
 * @param file Source file that detected the error.
 * @param line Source line that detected the error.
 */
void bsp_error_handler(const char *file, uint32_t line);
/** @brief Turn on one logical board LED. @param led Logical LED role. */
void bsp_led_on(bsp_led_t led);
/** @brief Turn off one logical board LED. @param led Logical LED role. */
void bsp_led_off(bsp_led_t led);
/** @brief Toggle one logical board LED. @param led Logical LED role. */
void bsp_led_toggle(bsp_led_t led);
/** @brief Assert the active-low W800 hardware reset output. */
void bsp_w800_reset_assert(void);
/** @brief Release the active-low W800 hardware reset output. */
void bsp_w800_reset_release(void);
/**
 * @brief Reset W800 with bounded assert and ready waits.
 * @param assert_ms Reset assertion time in milliseconds.
 * @param ready_ms Post-reset ready wait in milliseconds.
 */
void bsp_w800_hard_reset(uint32_t assert_ms, uint32_t ready_ms);
/**
 * @brief Read SPI NOR identity and emit one diagnostic line.
 * @param write_line Caller-provided line writer; NULL disables output.
 */
void bsp_spi_nor_log_id(void (*write_line)(const char *line));

#endif /* BSP_H */
