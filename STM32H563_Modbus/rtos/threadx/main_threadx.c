/**
 * @file main_threadx.c
 * @brief Static ThreadX startup and bounded Modbus RTU service tasks.
 */

#include "main.h"
#include "dcache.h"
#include "gpio.h"
#include "icache.h"

#include "bsp.h"
#include "bsp_fatal.h"
#include "bsp_led.h"
#include "bsp_pwm.h"
#include "modbus_app.h"
#include "tx_api.h"

#define MODBUS_THREAD_STACK_BYTES (2048U)
#define LED_THREAD_STACK_BYTES    (512U)

static TX_THREAD modbus_thread;
static TX_THREAD led_thread;
static ULONG modbus_thread_stack[MODBUS_THREAD_STACK_BYTES / sizeof(ULONG)];
static ULONG led_thread_stack[LED_THREAD_STACK_BYTES / sizeof(ULONG)];

static void system_clock_config(void);

/** @brief Run one bounded Modbus service step per ThreadX millisecond. */
static void modbus_thread_entry(ULONG input)
{
    bsp_status_t status;

    (void)input;
    status = modbus_app_init();
    if(status != BSP_STATUS_OK)
    {
        bsp_fatal_stop(BSP_FATAL_STAGE_RUNTIME, status);
    }

    while(1)
    {
        status = modbus_app_step(1000U);
        if(status != BSP_STATUS_OK)
        {
            bsp_fatal_stop(BSP_FATAL_STAGE_RUNTIME, status);
        }
        (void)tx_thread_sleep(1U);
    }
}

/** @brief Blink the board status LED without blocking protocol service. */
static void led_thread_entry(ULONG input)
{
    (void)input;
    while(1)
    {
        (void)bsp_led_toggle(BOARD_LED_STATUS);
        (void)tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND / 2U);
    }
}

/**
 * @brief Create all application tasks from caller-owned static storage.
 * @param first_unused_memory ThreadX-provided unused RAM marker; intentionally unused.
 */
void tx_application_define(void *first_unused_memory)
{
    UINT status;

    (void)first_unused_memory;
    status = tx_thread_create(&modbus_thread, "ld_modbus service",
                              modbus_thread_entry, 0U,
                              modbus_thread_stack, sizeof(modbus_thread_stack),
                              10U, 10U, TX_NO_TIME_SLICE, TX_AUTO_START);
    if(status != TX_SUCCESS)
    {
        bsp_fatal_stop(BSP_FATAL_STAGE_RUNTIME, BSP_STATUS_IO_ERROR);
    }

    status = tx_thread_create(&led_thread, "status led",
                              led_thread_entry, 0U,
                              led_thread_stack, sizeof(led_thread_stack),
                              20U, 20U, TX_NO_TIME_SLICE, TX_AUTO_START);
    if(status != TX_SUCCESS)
    {
        bsp_fatal_stop(BSP_FATAL_STAGE_RUNTIME, BSP_STATUS_IO_ERROR);
    }
}

/** @brief Initialize the board and transfer control to the ThreadX kernel. */
int main(void)
{
    bsp_pwm_config_t pwm_config = {1000U, 0U};
    bsp_status_t status;

    HAL_Init();
    system_clock_config();
    gpio_init();
    icache_init();
    dcache_init();

    status = bsp_init();
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        bsp_fatal_stop(BSP_FATAL_STAGE_BOARD, status);
    }
    status = bsp_pwm_init(BOARD_PWM_LCD_BACKLIGHT, &pwm_config, NULL);
    if(status != BSP_STATUS_OK)
    {
        bsp_fatal_stop(BSP_FATAL_STAGE_BOARD, status);
    }

    tx_kernel_enter();
    bsp_fatal_stop(BSP_FATAL_STAGE_RUNTIME, BSP_STATUS_IO_ERROR);
}

/** @brief Configure the 25 MHz HSE and PLL1 for a 250 MHz system clock. */
static void system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
    oscillator.PLL.PLLM = 2U;
    oscillator.PLL.PLLN = 40U;
    oscillator.PLL.PLLP = 2U;
    oscillator.PLL.PLLQ = 2U;
    oscillator.PLL.PLLR = 2U;
    oscillator.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
    oscillator.PLL.PLLFRACN = 0U;
    if(HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        Error_Handler();
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                      RCC_CLOCKTYPE_PCLK3;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    clock.APB3CLKDivider = RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/** @brief Convert an unrecoverable HAL initialization failure to BSP fatal handling. */
void Error_Handler(void)
{
    bsp_fatal_stop(BSP_FATAL_STAGE_CLOCK, BSP_STATUS_IO_ERROR);
}
