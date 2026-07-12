/**
 * @file mcu_cache.c
 * @brief STM32H563 instruction/data cache ownership and maintenance.
 */

#include "bsp_cache.h"

#include <stddef.h>

#include "stm32h5xx_hal.h"

static DCACHE_HandleTypeDef g_dcache;
static uint8_t g_cache_initialized;

/** @brief Implement bsp_cache_init() without CubeMX cache source files. */
bsp_status_t bsp_cache_init(void)
{
    if(g_cache_initialized != 0U)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    if(HAL_ICACHE_Enable() != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_RCC_DCACHE1_CLK_ENABLE();
    g_dcache.Instance = DCACHE1;
    g_dcache.Init.ReadBurstType = DCACHE_READ_BURST_WRAP;
    if(HAL_DCACHE_Init(&g_dcache) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    g_cache_initialized = 1U;
    return BSP_STATUS_OK;
}

/** @brief Implement D-Cache clean for one DMA transmit range. */
bsp_status_t bsp_cache_clean(const void *address, uint32_t length)
{
    if((address == NULL) || (length == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(g_cache_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_DCACHE_CleanByAddr(&g_dcache,
                                  (const uint32_t *)address,
                                  length) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Implement D-Cache invalidate for one DMA receive range. */
bsp_status_t bsp_cache_invalidate(void *address, uint32_t length)
{
    if((address == NULL) || (length == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(g_cache_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_DCACHE_InvalidateByAddr(&g_dcache,
                                       (uint32_t *)address,
                                       length) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}
