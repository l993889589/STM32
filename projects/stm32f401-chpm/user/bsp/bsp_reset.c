/**
 * @file bsp_reset.c
 * @brief STM32F401 reset-cause decoding and reset request.
 */

#include "bsp_reset.h"

#include "stm32f4xx_hal.h"

/** @brief Decode RCC reset flags into portable BSP bits. */
bsp_status_t bsp_reset_get_causes(uint32_t *causes)
{
    uint32_t value = 0U;

    if(causes == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)
        value |= BSP_RESET_CAUSE_PIN;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != RESET ||
       __HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)
        value |= BSP_RESET_CAUSE_BROWNOUT;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)
        value |= BSP_RESET_CAUSE_SOFTWARE;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
        value |= BSP_RESET_CAUSE_IWDG;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET)
        value |= BSP_RESET_CAUSE_WWDG;
    if(__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET)
        value |= BSP_RESET_CAUSE_LOW_POWER;
    *causes = value;
    return BSP_STATUS_OK;
}

/** @brief Clear every RCC reset-cause flag. */
void bsp_reset_clear_causes(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/** @brief Request an immediate system reset. */
void bsp_system_reset(void)
{
    NVIC_SystemReset();
}
