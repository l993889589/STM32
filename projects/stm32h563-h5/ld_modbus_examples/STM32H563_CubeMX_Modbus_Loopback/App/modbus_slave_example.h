/**
 * @file modbus_slave_example.h
 * @brief Minimal CubeMX USART2 Modbus RTU slave example interface.
 */

#ifndef MODBUS_SLAVE_EXAMPLE_H
#define MODBUS_SLAVE_EXAMPLE_H

#include <stdint.h>

#include "stm32h5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Small public diagnostic block suitable for a debugger Watch window. */
typedef struct
{
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t crc_errors;
    uint32_t protocol_errors;
    uint32_t t15_violations;
    uint32_t rx_overflows;
    uint32_t uart_errors;
    uint32_t t15_us;
    uint32_t t35_us;
} modbus_slave_example_report_t;

extern volatile modbus_slave_example_report_t g_modbus_slave_report;

/** @brief Initialize TIM2 framing time and arm USART2 one-byte reception. */
HAL_StatusTypeDef modbus_slave_example_init(void);

/** @brief Drain received bytes, complete RTU frames, and send slave replies. */
void modbus_slave_example_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_EXAMPLE_H */
