/**
 * @file modbus_app_config.h
 * @brief Public configuration for the app-only STM32H563 ld_modbus example.
 *
 * This file is intentionally small so an external user can change the UART,
 * unit id, or baud rate without learning the whole BSP. The default path does
 * not use LDC; LDC sources are kept in the project as an optional adapter.
 */

#ifndef MODBUS_APP_CONFIG_H
#define MODBUS_APP_CONFIG_H

#include "bsp_uart.h"

#define MODBUS_APP_USE_LDC              (0U)
#define MODBUS_APP_UNIT_ID              (1U)
#define MODBUS_APP_UART_ROLE            BOARD_UART_RS485_1
#define MODBUS_APP_UART_BAUDRATE        (115200U)
#define MODBUS_APP_UART_RX_CHUNK_BYTES  (1U)
#define MODBUS_APP_RTU_BITS_PER_CHAR    (11U)
#define MODBUS_APP_TX_TIMEOUT_MS        (100U)

#endif
