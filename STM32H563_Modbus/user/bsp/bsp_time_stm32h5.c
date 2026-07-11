/**
 * @file bsp_time_stm32h5.c
 * @brief STM32 HAL tick based time and deadline implementation.
 */

#include "bsp_time.h"

#include "stm32h5xx_hal.h"

/**
 * @brief Implement bsp_time_get_ms() as documented by its interface contract.
 */
uint32_t bsp_time_get_ms(void)
{
    return HAL_GetTick();
}

/**
 * @brief Implement bsp_time_elapsed_ms() as documented by its interface contract.
 */
uint32_t bsp_time_elapsed_ms(uint32_t started_at_ms)
{
    return bsp_time_get_ms() - started_at_ms;
}

/**
 * @brief Implement bsp_time_delay_ms() as documented by its interface contract.
 */
void bsp_time_delay_ms(uint32_t delay_ms)
{
    const uint32_t started_at_ms = bsp_time_get_ms();

    while(bsp_time_elapsed_ms(started_at_ms) < delay_ms)
    {
        __NOP();
    }
}

/**
 * @brief Implement bsp_deadline_start() as documented by its interface contract.
 */
void bsp_deadline_start(bsp_deadline_t *deadline, uint32_t timeout_ms)
{
    if(deadline != NULL)
    {
        deadline->expires_at_ms = bsp_time_get_ms() + timeout_ms;
    }
}

/**
 * @brief Implement bsp_deadline_has_expired() as documented by its interface contract.
 */
bool bsp_deadline_has_expired(const bsp_deadline_t *deadline)
{
    if(deadline == NULL)
    {
        return true;
    }

    return ((int32_t)(bsp_time_get_ms() - deadline->expires_at_ms) >= 0);
}
