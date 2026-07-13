/**
 * @file modbus_master_example.h
 * @brief Runtime-independent Modbus RTU master demonstration interface.
 */

#ifndef MODBUS_MASTER_EXAMPLE_H
#define MODBUS_MASTER_EXAMPLE_H

#include <stdint.h>

#include "stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Master transaction counters and last observed response values. */
typedef struct
{
    uint32_t requests_sent;
    uint32_t responses_received;
    uint32_t response_timeouts;
    uint32_t parse_errors;
    uint32_t t15_violations;
    uint16_t last_registers[2];
    uint8_t last_exception;
} modbus_master_report_t;

extern volatile modbus_master_report_t g_modbus_master_report;

/** @brief Initialize the periodic FC03 master demonstration. */
HAL_StatusTypeDef modbus_master_example_init(void);

/** @brief Advance receive processing and the bounded master state machine. */
void modbus_master_example_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_MASTER_EXAMPLE_H */
