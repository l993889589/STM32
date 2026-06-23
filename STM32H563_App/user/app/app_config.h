#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_FIRMWARE_VERSION                 "1.0.0"

#define APP_USB_LDC_MAX_FRAME              256U
#define APP_RS485_LDC_MAX_FRAME            256U
#define APP_RS485_MODBUS_UNIT_ID           1U
#define APP_RS485_MODBUS_HOLDING_COUNT     64U
#define APP_RS485_UART_BAUDRATE            115200U
#define APP_W800_UART_BAUDRATE             115200U
#define APP_NEARLINK_UART_BAUDRATE          115200U
#define APP_NEARLINK_DEFAULT_ROLE           1U
#define APP_NEARLINK_SERVER_NAME            "H563_SERVER"
#define APP_NEARLINK_CLIENT_NAME            "H563_CLIENT"
#define APP_NEARLINK_SERVER_ADDRESS         "111122220009"
#define APP_NEARLINK_CLIENT_ADDRESS         "111122220001"
#define APP_NEARLINK_LDC_MAX_FRAME           512U
#define APP_NEARLINK_RX_BUF_SIZE             1024U
#define APP_NEARLINK_UART_RX_BUF_SIZE        512U
#define APP_NEARLINK_PACKET_COUNT            8U
#define APP_W800_WIFI_SSID                 "CU_eaJU"
#define APP_W800_WIFI_PASSWORD             "cgzrte4s"
#define APP_W800_MQTT_HOST                 "192.168.1.4"
#define APP_W800_MQTT_PORT                 1883U

#define APP_USB_RX_BUF_SIZE                256U
#define APP_USB_PACKET_COUNT               8U
#define APP_RS485_RX_BUF_SIZE              256U
#define APP_RS485_UART_RX_BUF_SIZE         256U
#define APP_RS485_PACKET_COUNT             8U
#define APP_RS485_LDC_TICK_MS              1U
#define APP_RS485_TX_TIMEOUT_MS            100U
#define APP_W800_RX_BUF_SIZE               1024U
#define APP_W800_UART_RX_BUF_SIZE          256U
#define APP_W800_PACKET_COUNT              16U
#define APP_W800_LDC_TICK_MS               1U
#define APP_W800_TX_TIMEOUT_MS             1000U
#define APP_W800_CMD_TIMEOUT_MS            3000U
#define APP_W800_JOIN_TIMEOUT_MS           45000U
#define APP_W800_MQTT_KEEPALIVE_S          60U
#define APP_W800_MQTT_CLIENT_ID            "leduo-h563-w800"
#define APP_W800_MQTT_STATUS_TOPIC         "leduo/w800/status"
#define APP_W800_LOCAL_PORT_START          18830U
#define APP_W800_LOCAL_PORT_END            18930U
#define APP_LED_TOGGLE_TICKS               1000U

#define APP_OTA_MAGIC_0                    0x4CU
#define APP_OTA_MAGIC_1                    0x44U
#define APP_OTA_MAGIC_2                    0x4FU
#define APP_OTA_MAGIC_3                    0x54U
#define APP_OTA_HEADER_SIZE                16U
#define APP_OTA_MAX_PAYLOAD                224U
#define APP_OTA_STREAM_BUF_SIZE            (APP_OTA_HEADER_SIZE + APP_OTA_MAX_PAYLOAD + 2U)
#define APP_OTA_CMD_BEGIN                  1U
#define APP_OTA_CMD_DATA                   2U
#define APP_OTA_CMD_MANIFEST               3U
#define APP_OTA_CMD_END                    4U
#define APP_OTA_CMD_RESET                  5U
#define APP_OTA_STATUS_OK                  0U
#define APP_OTA_STATUS_BAD_FRAME           1U
#define APP_OTA_STATUS_BAD_CRC             2U
#define APP_OTA_STATUS_BAD_RANGE           3U
#define APP_OTA_STATUS_FLASH_ERROR         4U
#define APP_OTA_STATUS_SEQUENCE            5U

#endif /* APP_CONFIG_H */
