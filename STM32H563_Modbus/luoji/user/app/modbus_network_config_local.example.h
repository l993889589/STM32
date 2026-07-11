/**
 * @file modbus_network_config_local.example.h
 * @brief Template for private W800 network values used during hardware tests.
 *
 * Copy to modbus_network_config_local.h and fill local test values.
 * Never commit the copied file because it may contain Wi-Fi credentials.
 */
#define MODBUS_W800_ENABLE       (1U)
#define MODBUS_W800_ROLE         TRANSPORT_W800_TCP_SERVER
#define MODBUS_W800_SSID         "your-ssid"
#define MODBUS_W800_PASSWORD     "your-password"
#define MODBUS_W800_LOCAL_PORT   (1502U)
/* Client role also needs MODBUS_W800_REMOTE_HOST and REMOTE_PORT. */
