/**
 * @file modbus_loopback.h
 * @brief Automatic USART2 master to UART4 slave Modbus RTU test interface.
 */

#ifndef MODBUS_LOOPBACK_H
#define MODBUS_LOOPBACK_H

#include <stdint.h>

#include "stm32h5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Public execution states visible from the debugger. */
typedef enum
{
    MODBUS_LOOPBACK_STATE_STARTUP = 0,
    MODBUS_LOOPBACK_STATE_CONFIGURE_BAUD,
    MODBUS_LOOPBACK_STATE_READ_REQUEST,
    MODBUS_LOOPBACK_STATE_READ_WAIT,
    MODBUS_LOOPBACK_STATE_WRITE_REQUEST,
    MODBUS_LOOPBACK_STATE_WRITE_WAIT,
    MODBUS_LOOPBACK_STATE_BAD_CRC_REQUEST,
    MODBUS_LOOPBACK_STATE_BAD_CRC_WAIT,
    MODBUS_LOOPBACK_STATE_WRONG_UNIT_REQUEST,
    MODBUS_LOOPBACK_STATE_WRONG_UNIT_WAIT,
    MODBUS_LOOPBACK_STATE_T15_FIRST_PART,
    MODBUS_LOOPBACK_STATE_T15_GAP_WAIT,
    MODBUS_LOOPBACK_STATE_T15_SECOND_PART,
    MODBUS_LOOPBACK_STATE_T15_RESPONSE_WAIT,
    MODBUS_LOOPBACK_STATE_T35_RECOVERY_REQUEST,
    MODBUS_LOOPBACK_STATE_T35_RECOVERY_WAIT,
    MODBUS_LOOPBACK_STATE_NEXT_BAUD,
    MODBUS_LOOPBACK_STATE_PASS,
    MODBUS_LOOPBACK_STATE_FAIL
} modbus_loopback_state_t;

/** @brief Stable failure identifiers stored in the public test report. */
typedef enum
{
    MODBUS_LOOPBACK_ERROR_NONE = 0,
    MODBUS_LOOPBACK_ERROR_TIMER,
    MODBUS_LOOPBACK_ERROR_UART_RX_START,
    MODBUS_LOOPBACK_ERROR_UART_RECONFIGURE,
    MODBUS_LOOPBACK_ERROR_BUILD_REQUEST,
    MODBUS_LOOPBACK_ERROR_UART_TRANSMIT,
    MODBUS_LOOPBACK_ERROR_READ_TIMEOUT,
    MODBUS_LOOPBACK_ERROR_READ_VALUE,
    MODBUS_LOOPBACK_ERROR_WRITE_TIMEOUT,
    MODBUS_LOOPBACK_ERROR_WRITE_ECHO,
    MODBUS_LOOPBACK_ERROR_UNEXPECTED_BAD_CRC_RESPONSE,
    MODBUS_LOOPBACK_ERROR_UNEXPECTED_WRONG_UNIT_RESPONSE,
    MODBUS_LOOPBACK_ERROR_UNEXPECTED_T15_RESPONSE,
    MODBUS_LOOPBACK_ERROR_T35_RECOVERY_TIMEOUT,
    MODBUS_LOOPBACK_ERROR_RX_OVERFLOW,
    MODBUS_LOOPBACK_ERROR_UART_RUNTIME
} modbus_loopback_error_t;

/** @brief Volatile hardware-test results intended for a Keil Watch window. */
typedef struct
{
    uint32_t magic;
    modbus_loopback_state_t state;
    modbus_loopback_error_t last_error;
    uint32_t active_baud_rate;
    uint32_t t15_us;
    uint32_t t35_us;
    uint32_t passed_checks;
    uint32_t failed_checks;
    uint32_t completed_baud_rates;
    uint32_t master_rx_frames;
    uint32_t slave_rx_frames;
    uint32_t slave_tx_frames;
    uint32_t slave_bad_crc_frames;
    uint32_t slave_t15_violations;
    uint32_t uart_errors;
    uint32_t rx_overflows;
    uint16_t last_read_value;
    uint16_t last_written_value;
} modbus_loopback_report_t;

extern volatile modbus_loopback_report_t g_modbus_loopback_report;

/**
 * @brief Start TIM2, initialize both RTU receive paths, and arm UART interrupts.
 * @return HAL_OK on success; otherwise the report contains the failed stage.
 */
HAL_StatusTypeDef modbus_loopback_init(void);

/** @brief Run bounded slave processing and advance the automatic master test. */
void modbus_loopback_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_LOOPBACK_H */
