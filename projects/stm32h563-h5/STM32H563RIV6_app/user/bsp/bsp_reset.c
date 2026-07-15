/**
 * @file bsp_reset.c
 * @brief STM32H5 reset-status adapter for the portable BSP reset API.
 */

#include "bsp_reset.h"

#include "stm32h5xx_hal.h"

/** @brief Read latched reset causes without clearing them. */
bsp_status_t bsp_reset_get_causes(uint32_t *causes)
{
    uint32_t value = 0U;

    if(causes == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_PIN;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_BROWNOUT;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_SOFTWARE;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_IWDG;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_WWDG;
    }
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != 0U)
    {
        value |= BSP_RESET_CAUSE_LOW_POWER;
    }
    *causes = value;
    return BSP_STATUS_OK;
}

/** @brief Clear hardware reset flags after the caller has persisted them. */
void bsp_reset_clear_causes(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}
