#include "bsp.h"

#include <stdio.h>
#include "usart.h"

void bsp_init(void)
{
    static uint8_t initialized;

    if(initialized != 0U)
        return;

    bsp_uart_init();
    (void)bsp_uart_bind(BSP_UART4, &huart4, 1U, 0U);
    (void)bsp_uart_bind(BSP_USART3, &huart3, 1U, 0U);
    (void)bsp_dwt_init();
//    bsp_led_off(BSP_LED_STATUS);
//    bsp_led_init();


    initialized = 1U;
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

void bsp_ap6212_power_on(void)
{
    HAL_GPIO_WritePin(WIFI_REG_ON_GPIO_Port, WIFI_REG_ON_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BT_WAKE_GPIO_Port, BT_WAKE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BT_RST_N_GPIO_Port, BT_RST_N_Pin, GPIO_PIN_SET);
}
