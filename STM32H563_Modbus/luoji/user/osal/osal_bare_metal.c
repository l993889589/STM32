/**
 * @file osal_bare_metal.c
 * @brief Bare-metal OS abstraction implementation.
 */

#include "osal.h"

#include "bsp_time.h"
#include "stm32h5xx.h"

/**
 * @brief Implement osal_time_get_ms() as documented by its interface contract.
 */
uint32_t osal_time_get_ms(void)
{
    return bsp_time_get_ms();
}

/**
 * @brief Implement osal_sleep_ms() as documented by its interface contract.
 */
void osal_sleep_ms(uint32_t delay_ms)
{
    bsp_time_delay_ms(delay_ms);
}

/**
 * @brief Implement osal_yield() as documented by its interface contract.
 */
void osal_yield(void)
{
    __WFI();
}
