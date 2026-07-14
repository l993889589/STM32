#include "includes.h"
#include "app_wifi.h"

#include <string.h>

#define APP_WIFI_SCAN_TIMEOUT_TICKS 1500U

static TX_SEMAPHORE scan_complete_semaphore;
static wiced_scan_result_t scan_result;
static wiced_scan_result_t *scan_result_pointer;
static uint32_t scan_result_count;

static void app_wifi_scan_callback(wiced_scan_result_t **result,
                                   void *user_data,
                                   int32_t status);

wwd_result_t app_wifi_start_and_scan(void)
{
    wwd_result_t result;
    wiced_mac_t mac;
    char message[160];

    bsp_uart_write_string(BSP_UART_DEBUG,
                          "AP6212 WICED: starting firmware download\r\n");

    result = wwd_management_init(WICED_COUNTRY_CHINA, NULL);
    (void)snprintf(message,
                   sizeof(message),
                   "AP6212 WICED init: result=%ld oob_irq=%lu oob_level=%u\r\n",
                   (long)result,
                   (unsigned long)bsp_sdio_wifi_get_oob_interrupt_count(),
                   (unsigned int)bsp_sdio_wifi_get_oob_level());
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    if (result != WWD_SUCCESS)
    {
        return result;
    }

    memset(&mac, 0, sizeof(mac));
    result = wwd_wifi_get_mac_address(&mac, WWD_STA_INTERFACE);
    (void)snprintf(message,
                   sizeof(message),
                   "AP6212 MAC: %02X:%02X:%02X:%02X:%02X:%02X result=%ld\r\n",
                   mac.octet[0], mac.octet[1], mac.octet[2],
                   mac.octet[3], mac.octet[4], mac.octet[5],
                   (long)result);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    if (result != WWD_SUCCESS)
    {
        return result;
    }

    if (tx_semaphore_create(&scan_complete_semaphore,
                            "wifi_scan_complete",
                            0U) != TX_SUCCESS)
    {
        return WWD_SEMAPHORE_ERROR;
    }

    memset(&scan_result, 0, sizeof(scan_result));
    scan_result_pointer = &scan_result;
    scan_result_count = 0U;

    result = wwd_wifi_scan(WICED_SCAN_TYPE_ACTIVE,
                           WICED_BSS_TYPE_ANY,
                           NULL,
                           NULL,
                           NULL,
                           NULL,
                           app_wifi_scan_callback,
                           &scan_result_pointer,
                           NULL,
                           WWD_STA_INTERFACE);
    (void)snprintf(message,
                   sizeof(message),
                   "AP6212 scan start: result=%ld\r\n",
                   (long)result);
    bsp_uart_write_string(BSP_UART_DEBUG, message);

    if (result == WWD_SUCCESS)
    {
        if (tx_semaphore_get(&scan_complete_semaphore,
                             APP_WIFI_SCAN_TIMEOUT_TICKS) != TX_SUCCESS)
        {
            result = WWD_TIMEOUT;
        }
    }

    (void)snprintf(message,
                   sizeof(message),
                   "AP6212 scan complete: result=%ld access_points=%lu\r\n",
                   (long)result,
                   (unsigned long)scan_result_count);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    (void)tx_semaphore_delete(&scan_complete_semaphore);

    return result;
}

static void app_wifi_scan_callback(wiced_scan_result_t **result,
                                   void *user_data,
                                   int32_t status)
{
    char message[160];
    uint8_t ssid_length;

    (void)user_data;

    if ((status == WICED_SCAN_INCOMPLETE) &&
        (result != NULL) && (*result != NULL))
    {
        ssid_length = (*result)->ssid.length;
        if (ssid_length > sizeof((*result)->ssid.value))
        {
            ssid_length = sizeof((*result)->ssid.value);
        }

        (void)snprintf(message,
                       sizeof(message),
                       "WiFi AP[%lu]: %.*s ch=%u rssi=%d security=%08lX\r\n",
                       (unsigned long)scan_result_count,
                       (int)ssid_length,
                       (const char *)(*result)->ssid.value,
                       (unsigned int)(*result)->channel,
                       (int)(*result)->signal_strength,
                       (unsigned long)(*result)->security);
        bsp_uart_write_string(BSP_UART_DEBUG, message);
        scan_result_count++;
        memset(*result, 0, sizeof(**result));
        return;
    }

    (void)tx_semaphore_put(&scan_complete_semaphore);
}
