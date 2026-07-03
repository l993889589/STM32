#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_FIRMWARE_VERSION                 "1.0.0"

/*
 * Optional application Message Bus.
 *
 * 0: keep the current direct service path. This is the lowest-overhead mode
 *    for small device counts.
 * 1: enable a central static message dispatcher above LDC endpoints. LDC still
 *    owns byte/block buffering and framing; the bus only routes upper-layer
 *    events, control requests and logs.
 */
#define APP_ENABLE_MSG_BUS                 0U
#define APP_MSG_BUS_HIGH_QUEUE_DEPTH       8U
#define APP_MSG_BUS_NORMAL_QUEUE_DEPTH     32U
#define APP_MSG_BUS_HANDLER_COUNT          16U

/*
 * Background logs are useful while bringing up modules, but they make the USB
 * shell hard to use because asynchronous task output interleaves with typing.
 *
 * 0: default quiet mode. Shell command responses are still printed.
 * 1: enable automatic W800/NearLink/RS485/USB/OTA diagnostic prints.
 */
#define APP_ENABLE_BACKGROUND_LOG          0U

#define APP_ENABLE_NEARLINK_LOG            APP_ENABLE_BACKGROUND_LOG
#define APP_ENABLE_RS485_LOG               APP_ENABLE_BACKGROUND_LOG
#define APP_ENABLE_USB_RAW_LOG             APP_ENABLE_BACKGROUND_LOG
#define APP_ENABLE_OTA_LOG                 APP_ENABLE_BACKGROUND_LOG
#define APP_ENABLE_USBX_DEVICE             0U
#define APP_ENABLE_UART4_LDC_TEST          0U
#define APP_ENABLE_AP6212_BT_LDC_BRIDGE    1U
#define APP_ENABLE_AP6212_BRINGUP          0U
#define APP_ENABLE_AP6212_NETXDUO          0U
#define APP_AP6212_NET_TRACE               0U

#if APP_ENABLE_AP6212_NETXDUO && !APP_ENABLE_AP6212_BRINGUP
#error "APP_ENABLE_AP6212_NETXDUO requires APP_ENABLE_AP6212_BRINGUP"
#endif

#define APP_SHELL_UART_PORT                BSP_UART_DEBUG
#define APP_SHELL_UART_NAME                "UART4"
#define APP_SHELL_UART_BAUDRATE            115200U
#define APP_SHELL_UART_BAUDRATE_TEXT       "115200"
#define APP_SHELL_RX_BUF_SIZE              64U
#define APP_SHELL_QUEUE_DEPTH              128U
#define APP_SHELL_STACK_SIZE               1024U
#define APP_SHELL_THREAD_PRIO              15U
#define APP_SHELL_TX_TIMEOUT_MS            100U

#define APP_UART4_ECHO_PORT                BSP_UART4
#define APP_UART4_ECHO_BAUDRATE            115200U
#define APP_UART4_ECHO_RX_DMA_SIZE         128U
#define APP_UART4_ECHO_RX_DMA              1U
#define APP_UART4_ECHO_TX_QUEUE_DEPTH      512U
#define APP_UART4_ECHO_TX_CHUNK_SIZE       64U
#define APP_UART4_ECHO_LDC_RING_SIZE       512U
#define APP_UART4_ECHO_LDC_PACKET_COUNT    8U
#define APP_UART4_ECHO_LDC_MAX_FRAME       256U
#define APP_UART4_ECHO_LDC_TIMEOUT_MS      20U
#define APP_UART4_ECHO_STACK_SIZE          1024U
#define APP_UART4_ECHO_THREAD_PRIO         14U

#define APP_AP6212_BT_UART_PORT            BSP_USART3
#define APP_AP6212_BT_UART_BAUDRATE        115200U
#define APP_AP6212_BT_RX_DMA_SIZE          256U
#define APP_AP6212_BT_TX_QUEUE_DEPTH       512U
#define APP_AP6212_BT_TX_CHUNK_SIZE        64U
#define APP_AP6212_BT_LDC_RING_SIZE        1024U
#define APP_AP6212_BT_LDC_PACKET_COUNT     8U
#define APP_AP6212_BT_LDC_MAX_FRAME        256U
#define APP_AP6212_BT_LDC_TIMEOUT_MS       20U
#define APP_AP6212_BT_STACK_SIZE           1024U
#define APP_AP6212_BT_THREAD_PRIO          13U

#define APP_AP6212_SDIO_PROBE_STACK_SIZE   1024U
#define APP_AP6212_SDIO_PROBE_THREAD_PRIO  16U
#define APP_AP6212_SDIO_PROBE_DELAY_MS     300U
#define APP_AP6212_WIFI_SSID               APP_W800_WIFI_SSID
#define APP_AP6212_WIFI_PASSWORD           APP_W800_WIFI_PASSWORD
#define APP_AP6212_ENABLE_RAW_NET_SMOKE    (APP_ENABLE_AP6212_NETXDUO ? 0U : 1U)
#define APP_AP6212_NETX_THREAD_STACK_SIZE  4096U
#define APP_AP6212_NETX_THREAD_PRIO        17U
#define APP_AP6212_NETX_RX_STACK_SIZE      2048U
#define APP_AP6212_NETX_RX_PRIO            12U
#define APP_AP6212_NETX_IP_STACK_SIZE      4096U
#define APP_AP6212_NETX_IP_PRIO            8U
#define APP_AP6212_NETX_PACKET_PAYLOAD     1600U
#define APP_AP6212_NETX_PACKET_POOL_SIZE   24576U
#define APP_AP6212_NETX_ARP_CACHE_SIZE     1024U

#define APP_USB_LDC_MAX_FRAME              256U
#define APP_RS485_LDC_MAX_FRAME            256U
#define APP_RS485_MODBUS_UNIT_ID           1U
#define APP_RS485_MODBUS_HOLDING_COUNT     64U
#define APP_RS485_UART_BAUDRATE            115200U
#define APP_RS485_FRAME_GAP_MS              ((38500U + APP_RS485_UART_BAUDRATE - 1U) / APP_RS485_UART_BAUDRATE)
#define APP_W800_UART_BAUDRATE             115200U
#define APP_NEARLINK_UART_BAUDRATE          115200U
#define APP_NEARLINK_BOOT_WAIT_MS           5000U
#define APP_NEARLINK_PROBE_TIMEOUT_MS       3000U
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
#define APP_RS485_TX_TIMEOUT_MS            100U
#define APP_W800_RX_BUF_SIZE               1024U
#define APP_W800_UART_RX_BUF_SIZE          256U
#define APP_W800_PACKET_COUNT              16U
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
