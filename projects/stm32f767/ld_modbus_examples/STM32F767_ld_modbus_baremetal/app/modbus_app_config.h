/**
 * @file modbus_app_config.h
 * @brief User-editable role, serial, and demonstration settings.
 */

#ifndef MODBUS_APP_CONFIG_H
#define MODBUS_APP_CONFIG_H

#define MODBUS_APP_ROLE_SLAVE  (1U)
#define MODBUS_APP_ROLE_MASTER (2U)

/* Select exactly one role. Slave is the easiest first hardware test. */
#ifndef MODBUS_APP_ROLE
#define MODBUS_APP_ROLE MODBUS_APP_ROLE_SLAVE
#endif

#if (MODBUS_APP_ROLE != MODBUS_APP_ROLE_SLAVE) && \
    (MODBUS_APP_ROLE != MODBUS_APP_ROLE_MASTER)
#error "Unsupported MODBUS_APP_ROLE"
#endif

/* Supported values: 9600, 19200, or 115200. */
#ifndef MODBUS_APP_BAUD_RATE
#define MODBUS_APP_BAUD_RATE (115200U)
#endif

#if (MODBUS_APP_BAUD_RATE != 9600U) && \
    (MODBUS_APP_BAUD_RATE != 19200U) && \
    (MODBUS_APP_BAUD_RATE != 115200U)
#error "MODBUS_APP_BAUD_RATE must be 9600, 19200, or 115200"
#endif

#define MODBUS_APP_BITS_PER_CHAR      (10U) /* 8 data, no parity, 1 stop. */
#define MODBUS_APP_UNIT_ID            (1U)
#define MODBUS_APP_TX_TIMEOUT_MS      (100U)
#define MODBUS_MASTER_PERIOD_MS       (1000U)
#define MODBUS_MASTER_RESPONSE_MS     (1000U)
#define MODBUS_MASTER_READ_ADDRESS    (0U)
#define MODBUS_MASTER_READ_QUANTITY   (2U)

#endif /* MODBUS_APP_CONFIG_H */
