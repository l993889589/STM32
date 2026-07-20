/**
 * @file bsp_onewire.h
 * @brief Single-owner PB0 1-Wire bus interface for board devices.
 */

#ifndef BSP_ONEWIRE_H
#define BSP_ONEWIRE_H

#include <stddef.h>
#include <stdint.h>

#include "bsp_status.h"

/**
 * @brief Initialize PB0 as the released open-drain 1-Wire bus.
 * @return BSP initialization status.
 */
bsp_status_t bsp_onewire_init(void);

/**
 * @brief Generate a reset pulse and validate a device presence pulse.
 * @return OK when a device responds, IO_ERROR when no presence is detected.
 * @note Task-context only; the sensor owner must serialize complete transactions.
 */
bsp_status_t bsp_onewire_reset(void);

/**
 * @brief Write bytes using least-significant-bit-first 1-Wire slots.
 * @param data Source bytes valid until the function returns.
 * @param length Number of bytes to write.
 * @return OK or a typed validation/readiness status.
 * @note Task-context only; the sensor owner must serialize complete transactions.
 */
bsp_status_t bsp_onewire_write(const uint8_t *data, size_t length);

/**
 * @brief Read bytes using least-significant-bit-first 1-Wire slots.
 * @param data Destination valid until the function returns.
 * @param length Number of bytes to read.
 * @return OK or a typed validation/readiness status.
 * @note Task-context only; the sensor owner must serialize complete transactions.
 */
bsp_status_t bsp_onewire_read(uint8_t *data, size_t length);

#endif /* BSP_ONEWIRE_H */
