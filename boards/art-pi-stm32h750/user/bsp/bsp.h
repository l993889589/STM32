#ifndef BSP_H
#define BSP_H

#include <stdint.h>

#include "main.h"


#include "bsp_led.h"
#include "bsp_dwt.h"
#include "bsp_pwm.h"
#include "bsp_uart.h"


#define BSP_VERSION             "0.1.0"

#define BSP_LED_STATUS_PORT     GPIOI
#define BSP_LED_STATUS_PIN      GPIO_PIN_8


typedef enum
{
    BSP_LED_STATUS = 0
} bsp_led_t;

void bsp_init(void);
void bsp_led_on(bsp_led_t led);
void bsp_led_off(bsp_led_t led);
void bsp_led_toggle(bsp_led_t led);
void bsp_ap6212_power_on(void);
void bsp_w800_reset_assert(void);
void bsp_w800_reset_release(void);
void bsp_w800_hard_reset(uint32_t assert_ms, uint32_t ready_ms);
void bsp_nearlink_reset_assert(void);
void bsp_nearlink_reset_release(void);
void bsp_nearlink_hard_reset(uint32_t assert_ms, uint32_t ready_ms);
void bsp_spi_nor_log_id(void (*write_line)(const char *line));

#endif /* BSP_H */
