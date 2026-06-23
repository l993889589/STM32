#include "bsp.h"

#include <stdio.h>
#include "spi.h"
#include "usart.h"

void bsp_init(void)
{
    (void)bsp_uart_bind(BSP_UART_W800_AT, &huart1, 1U, 1U);
    (void)bsp_uart_bind(BSP_UART_RS485, &huart2, 0U, 0U);
    (void)bsp_uart_bind(BSP_UART_NEARLINK, &huart3, 0U, 0U);
    (void)gd25lq128_bind(&hspi1);

    (void)bsp_dwt_init();

    bsp_timer_init();
    bsp_uart_init();
    bsp_w800_reset_release();
    bsp_led_off(BSP_LED_STATUS);
		bsp_led_init();
}

void bsp_led_on(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_WritePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN, GPIO_PIN_SET);
}

void bsp_led_off(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_WritePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN, GPIO_PIN_RESET);
}

void bsp_led_toggle(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_TogglePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN);
}

void bsp_w800_reset_assert(void)
{
    HAL_GPIO_WritePin(BSP_W800_RESET_PORT, BSP_W800_RESET_PIN, GPIO_PIN_RESET);
}

void bsp_w800_reset_release(void)
{
    HAL_GPIO_WritePin(BSP_W800_RESET_PORT, BSP_W800_RESET_PIN, GPIO_PIN_SET);
}

void bsp_w800_hard_reset(uint32_t assert_ms, uint32_t ready_ms)
{
    bsp_w800_reset_assert();
    HAL_Delay(assert_ms);
    bsp_w800_reset_release();
    HAL_Delay(ready_ms);
}

void bsp_spi_nor_log_id(void (*write_line)(const char *line))
{
    gd25lq128_id_t id;
    char line[96];

    if(!write_line)
        return;

    if(gd25lq128_read_id(&id))
    {
        (void)snprintf(line, sizeof(line),
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
