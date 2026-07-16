/**
 * @file bsp_status.h
 * @brief Shared BSP status-code domain.
 */

#ifndef BSP_STATUS_H
#define BSP_STATUS_H

/** @brief Result codes shared by MCU, board, and device layers. */
typedef enum
{
    BSP_STATUS_OK = 0,
    BSP_STATUS_ALREADY_INITIALIZED,
    BSP_STATUS_INVALID_ARGUMENT,
    BSP_STATUS_NOT_READY,
    BSP_STATUS_BUSY,
    BSP_STATUS_TIMEOUT,
    BSP_STATUS_IO_ERROR,
    BSP_STATUS_OVERFLOW,
    BSP_STATUS_CONFLICT,
    BSP_STATUS_NOT_SUPPORTED,
    BSP_STATUS_DEGRADED
} bsp_status_t;

#endif /* BSP_STATUS_H */
