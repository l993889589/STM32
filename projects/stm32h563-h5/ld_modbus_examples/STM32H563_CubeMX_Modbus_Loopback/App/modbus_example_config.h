/**
 * @file modbus_example_config.h
 * @brief Select the small slave example or the full hardware loopback test.
 */

#ifndef MODBUS_EXAMPLE_CONFIG_H
#define MODBUS_EXAMPLE_CONFIG_H

#define MODBUS_EXAMPLE_MODE_SLAVE     (1U)
#define MODBUS_EXAMPLE_MODE_LOOPBACK  (2U)

/* Default public entry: one USART2 RTU slave that is easy to port. */
#ifndef MODBUS_EXAMPLE_MODE
#define MODBUS_EXAMPLE_MODE MODBUS_EXAMPLE_MODE_SLAVE
#endif

#if (MODBUS_EXAMPLE_MODE != MODBUS_EXAMPLE_MODE_SLAVE) && \
    (MODBUS_EXAMPLE_MODE != MODBUS_EXAMPLE_MODE_LOOPBACK)
#error "Unsupported MODBUS_EXAMPLE_MODE"
#endif

#define MODBUS_SLAVE_UNIT_ID          (1U)
#define MODBUS_SLAVE_BAUD_RATE        (115200U)
#define MODBUS_SLAVE_BITS_PER_CHAR    (10U)
#define MODBUS_SLAVE_UART_TIMEOUT_MS  (100U)
#define MODBUS_SLAVE_RX_RING_BYTES    (512U)
#define MODBUS_SLAVE_HOLDING_COUNT    (32U)

#endif /* MODBUS_EXAMPLE_CONFIG_H */
