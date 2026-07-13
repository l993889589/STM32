/**
 * @file modbus_loopback_config.h
 * @brief User-editable settings for the USART2/UART4 Modbus RTU loopback test.
 */

#ifndef MODBUS_LOOPBACK_CONFIG_H
#define MODBUS_LOOPBACK_CONFIG_H

#define MODBUS_LOOPBACK_UNIT_ID              (1U)
#define MODBUS_LOOPBACK_BITS_PER_CHAR        (10U)
#define MODBUS_LOOPBACK_UART_TIMEOUT_MS      (100U)
#define MODBUS_LOOPBACK_RESPONSE_TIMEOUT_MS  (250U)
#define MODBUS_LOOPBACK_NEGATIVE_WAIT_MS     (120U)
#define MODBUS_LOOPBACK_STARTUP_WAIT_MS      (100U)
#define MODBUS_LOOPBACK_RX_RING_BYTES        (512U)
#define MODBUS_LOOPBACK_HOLDING_COUNT        (32U)

#endif /* MODBUS_LOOPBACK_CONFIG_H */
