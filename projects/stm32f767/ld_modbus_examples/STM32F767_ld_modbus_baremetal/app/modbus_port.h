/**
 * @file modbus_port.h
 * @brief CubeMX USART3 and SysTick adapter shared by both runtime examples.
 */

#ifndef MODBUS_PORT_H
#define MODBUS_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Public receive diagnostics suitable for a debugger Watch window. */
typedef struct
{
    uint32_t received_bytes;
    uint32_t ring_overflows;
    uint32_t uart_errors;
    uint32_t rx_rearm_errors;
} modbus_port_diag_t;

extern volatile modbus_port_diag_t g_modbus_port_diag;

/**
 * @brief Configure USART3 baud rate and start one-byte interrupt reception.
 * @param baud_rate Must match one of the application-supported baud rates.
 * @return HAL_OK when USART3 is configured and reception is armed.
 */
HAL_StatusTypeDef modbus_port_init(uint32_t baud_rate);

/**
 * @brief Return a wrapping microsecond timestamp derived from 1 ms SysTick.
 * @note Safe in thread, superloop, and interrupt context; no extra timer used.
 */
uint32_t modbus_port_time_us(void);

/**
 * @brief Pop one received byte and its end-of-character timestamp.
 * @param byte Receives the oldest byte.
 * @param timestamp_us Receives the matching wrapping microsecond timestamp.
 * @return True when an entry was available.
 */
bool modbus_port_try_read(uint8_t *byte, uint32_t *timestamp_us);

/**
 * @brief Transmit a complete RTU ADU through USART3 using a bounded HAL wait.
 * @param data Complete ADU storage valid for the duration of this call.
 * @param length Number of bytes to transmit.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return HAL transmit status.
 */
HAL_StatusTypeDef modbus_port_write(const uint8_t *data,
                                    uint16_t length,
                                    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_PORT_H */
