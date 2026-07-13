/**
 * @file modbus_slave_example.h
 * @brief Runtime-independent Modbus RTU slave demonstration interface.
 */

#ifndef MODBUS_SLAVE_EXAMPLE_H
#define MODBUS_SLAVE_EXAMPLE_H

#include <stdint.h>

#include "stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Slave counters and active timing values for debugger inspection. */
typedef struct
{
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t crc_errors;
    uint32_t protocol_errors;
    uint32_t t15_violations;
    uint32_t t15_us;
    uint32_t t35_us;
} modbus_slave_report_t;

extern volatile modbus_slave_report_t g_modbus_slave_report;

/** @brief Initialize static maps, strict RTU framing, and USART3 reception. */
HAL_StatusTypeDef modbus_slave_example_init(void);

/** @brief Drain received bytes and process every completed slave request. */
void modbus_slave_example_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_EXAMPLE_H */
