/**
 * @file sensor_status.h
 * @brief Portable status values shared by the pure C sensor drivers.
 */

#ifndef SENSOR_STATUS_H
#define SENSOR_STATUS_H

typedef enum
{
    SENSOR_STATUS_OK = 0,
    SENSOR_STATUS_ALREADY_INITIALIZED,
    SENSOR_STATUS_INVALID_ARGUMENT,
    SENSOR_STATUS_NOT_READY,
    SENSOR_STATUS_BUSY,
    SENSOR_STATUS_TIMEOUT,
    SENSOR_STATUS_IO_ERROR,
    SENSOR_STATUS_NOT_PRESENT,
    SENSOR_STATUS_CRC_ERROR,
    SENSOR_STATUS_RANGE_ERROR
} sensor_status_t;

#endif /* SENSOR_STATUS_H */
