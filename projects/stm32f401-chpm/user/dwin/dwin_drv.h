/**
 * @file dwin_drv.h
 * @brief Legacy DWIN write API retained as a dwin_tx submission adapter.
 */

#ifndef DWIN_DRV_H
#define DWIN_DRV_H

#include <stdint.h>

/**
 * @brief Submit one ordered variable write to the unique DWIN TX owner.
 * @param address DWIN variable address.
 * @param data Payload copied before return.
 * @param length Payload size in bytes.
 * @param timeout_ms Retained for source compatibility; no synchronous wait.
 * @return One when admitted, otherwise zero.
 */
uint8_t dwin_write_block(uint16_t address,
                         uint8_t *data,
                         uint8_t length,
                         uint16_t timeout_ms);

#endif /* DWIN_DRV_H */
