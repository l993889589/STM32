/**
 * @file main.c
 * @brief Bootloader-addressed Modbus demo startup and bounded superloop entry.
 */

#include "main.h"
#include <stdbool.h>
#include "dcache.h"
#include "gpio.h"
#include "icache.h"

#include "bsp.h"
#include "bsp_stop.h"
#include "bsp_led.h"
#include "bsp_pwm.h"
#include "bsp_time.h"
#include "modbus_app.h"
#include "modbus_network_app.h"
#include "osal.h"

static void system_clock_config(void);

/**
 * @brief Initialize the bare-metal platform and run the bounded service superloop.
 */
int main(void)
{
    bsp_deadline_t led_deadline;
    bsp_deadline_t service_deadline;
    bsp_deadline_t network_deadline;
    bsp_deadline_t network_retry_deadline;
    bsp_pwm_config_t pwm_config = {1000U, 0U};
    bsp_status_t status;
    bool network_ready;

    HAL_Init();
    system_clock_config();

    gpio_init();
    icache_init();
    dcache_init();

    status = bsp_init();
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        bsp_stop_on_error(BSP_ERROR_STAGE_BOARD, status);
    }

    status = bsp_pwm_init(BOARD_PWM_LCD_BACKLIGHT, &pwm_config, NULL);
    if(status != BSP_STATUS_OK)
    {
        bsp_stop_on_error(BSP_ERROR_STAGE_BOARD, status);
    }

    status = modbus_app_init();
    if(status != BSP_STATUS_OK)
    {
        bsp_stop_on_error(BSP_ERROR_STAGE_RUNTIME, status);
    }
    status = modbus_network_app_init();
    network_ready = (status == BSP_STATUS_OK) ||
                    (status == BSP_STATUS_ALREADY_INITIALIZED);

    bsp_deadline_start(&led_deadline, 500U);
    bsp_deadline_start(&service_deadline, 1U);
    bsp_deadline_start(&network_deadline, 10U);
    bsp_deadline_start(&network_retry_deadline, 5000U);

    while(1)
    {
        if(bsp_deadline_has_expired(&led_deadline))
        {
            (void)bsp_led_toggle(BOARD_LED_STATUS);
            bsp_deadline_start(&led_deadline, 500U);
        }

        if(bsp_deadline_has_expired(&service_deadline))
        {
            status = modbus_app_step(1000U);
            if(status != BSP_STATUS_OK)
            {
                bsp_stop_on_error(BSP_ERROR_STAGE_RUNTIME, status);
            }
            bsp_deadline_start(&service_deadline, 1U);
        }

        if(network_ready && bsp_deadline_has_expired(&network_deadline))
        {
            status = modbus_network_app_step(10U);
            if((status != BSP_STATUS_OK) && (status != BSP_STATUS_NOT_READY))
            {
                (void)modbus_network_app_deinit();
                network_ready = false;
                bsp_deadline_start(&network_retry_deadline, 5000U);
            }
            bsp_deadline_start(&network_deadline, 10U);
        }
        else if(!network_ready && bsp_deadline_has_expired(&network_retry_deadline))
        {
            status = modbus_network_app_init();
            network_ready = (status == BSP_STATUS_OK) ||
                            (status == BSP_STATUS_ALREADY_INITIALIZED);
            bsp_deadline_start(&network_retry_deadline, 5000U);
        }

        osal_yield();
    }
}

/**
 * @brief Configure the 25 MHz HSE and PLL1 for a 250 MHz system clock.
 */
static void system_clock_config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 40;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2 |
                                  RCC_CLOCKTYPE_PCLK3;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
 * @brief Convert an unrecoverable Cube/HAL initialization error into BSP safe-stop handling.
 */
void Error_Handler(void)
{
    bsp_stop_on_error(BSP_ERROR_STAGE_CLOCK, BSP_STATUS_IO_ERROR);
}

#ifdef USE_FULL_ASSERT
/**
 * @brief Convert a HAL assertion into BSP safe-stop handling.
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    bsp_stop_on_error(BSP_ERROR_STAGE_RUNTIME, BSP_STATUS_INVALID_ARGUMENT);
}
#endif
