/**
 * @file bsp_cache.h
 * @brief Cache initialization and DMA-coherency maintenance interface.
 */

#ifndef BSP_CACHE_H
#define BSP_CACHE_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief STM32H563 data-cache line size used by DMA buffer contracts. */
#define BSP_CACHE_LINE_SIZE 32U

/** @brief Initialize instruction and data caches. @return BSP status. */
bsp_status_t bsp_cache_init(void);
/** @brief Clean a caller-owned DMA transmit range from D-Cache. */
bsp_status_t bsp_cache_clean(const void *address, uint32_t length);
/** @brief Invalidate a caller-owned DMA receive range from D-Cache. */
bsp_status_t bsp_cache_invalidate(void *address, uint32_t length);
/** @brief Check whether a buffer starts and ends on cache-line boundaries. */
uint8_t bsp_cache_is_line_aligned(const void *address, uint32_t length);

#endif /* BSP_CACHE_H */
