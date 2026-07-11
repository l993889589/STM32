/**
 * @file modbus_network_config.h
 * @brief Committed, credential-free defaults for the optional W800 demo.
 *
 * Create modbus_network_config_local.h beside this file to enable hardware.
 * That local header is ignored by Git and may override every macro below.
 */

#ifndef MODBUS_NETWORK_CONFIG_H
#define MODBUS_NETWORK_CONFIG_H

#if defined(__has_include)
#if __has_include("modbus_network_config_local.h")
#include "modbus_network_config_local.h"
#endif
#endif

#ifndef MODBUS_W800_ENABLE
#define MODBUS_W800_ENABLE               (0U)
#endif
#ifndef MODBUS_W800_ROLE
#define MODBUS_W800_ROLE                 TRANSPORT_W800_TCP_SERVER
#endif
#ifndef MODBUS_W800_SSID
#define MODBUS_W800_SSID                 ""
#endif
#ifndef MODBUS_W800_PASSWORD
#define MODBUS_W800_PASSWORD             ""
#endif
#ifndef MODBUS_W800_REMOTE_HOST
#define MODBUS_W800_REMOTE_HOST          "192.168.1.2"
#endif
#ifndef MODBUS_W800_REMOTE_PORT
#define MODBUS_W800_REMOTE_PORT          (1502U)
#endif
#ifndef MODBUS_W800_LOCAL_PORT
#define MODBUS_W800_LOCAL_PORT           (1502U)
#endif
#ifndef MODBUS_W800_UART_BAUD_RATE
#define MODBUS_W800_UART_BAUD_RATE       (115200U)
#endif
#ifndef MODBUS_W800_SERVER_IDLE_TIMEOUT_S
#define MODBUS_W800_SERVER_IDLE_TIMEOUT_S (300U)
#endif

#endif
