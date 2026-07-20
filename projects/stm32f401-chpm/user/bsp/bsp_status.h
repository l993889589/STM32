/**
 * @file bsp_status.h
 * @brief Common status values returned by CHPM BSP modules.
 */

#ifndef BSP_STATUS_H
#define BSP_STATUS_H

typedef enum
{
    BSP_STATUS_OK = 0,
    BSP_STATUS_ALREADY_INITIALIZED,
    BSP_STATUS_INVALID_ARGUMENT,
    BSP_STATUS_TIMEOUT,
    BSP_STATUS_BUSY,
    BSP_STATUS_NOT_READY,
    BSP_STATUS_CONFLICT,
    BSP_STATUS_IO_ERROR,
    BSP_STATUS_OVERFLOW,
    BSP_STATUS_NOT_SUPPORTED
} bsp_status_t;

#endif
