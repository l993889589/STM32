#ifndef AP6212_WICED_H
#define AP6212_WICED_H

#include <stdint.h>

/*
 * Minimal ABI surface for the official ART-Pi WICED 3.3.1 binary.
 * These layouts were checked against the DWARF information in the library.
 * Project-owned APIs use lower_snake_case; vendor entry points keep their
 * original names because they are part of the prebuilt library ABI.
 */

typedef int32_t wwd_result_t;
typedef void *wiced_buffer_t;

typedef struct
{
    void *object;
} host_thread_type_t;

typedef struct
{
    void *object;
} host_semaphore_type_t;

typedef struct
{
    void *object;
    int32_t message_length;
} host_queue_type_t;

typedef struct
{
    uint8_t length;
    uint8_t value[32];
} wiced_ssid_t;

typedef struct
{
    uint8_t octet[6];
} wiced_mac_t;

#pragma pack(push, 1)
typedef struct wiced_scan_result
{
    wiced_ssid_t ssid;
    wiced_mac_t bssid;
    int16_t signal_strength;
    uint32_t max_data_rate;
    int8_t bss_type;
    uint32_t security;
    uint8_t channel;
    uint8_t band;
    uint8_t on_channel;
    struct wiced_scan_result *next;
} wiced_scan_result_t;
#pragma pack(pop)

typedef void (*wiced_scan_result_callback_t)(wiced_scan_result_t **result,
                                             void *user_data,
                                             int32_t status);

enum
{
    WWD_SUCCESS = 0,
    WWD_PENDING = 1,
    WWD_TIMEOUT = 2,
    WWD_SEMAPHORE_ERROR = 1014,
    WWD_QUEUE_ERROR = 1032,
    WWD_BUFFER_POINTER_MOVE_ERROR = 1033,
    WWD_BUFFER_SIZE_SET_ERROR = 1034,
    WWD_THREAD_STACK_NULL = 1035,
    WWD_THREAD_DELETE_FAIL = 1036,
    WWD_SLEEP_ERROR = 1037,
    WWD_BUFFER_ALLOC_FAIL = 1038,
    WWD_THREAD_FINISH_FAIL = 1053,
    WWD_WAIT_ABORTED = 1054,
    WWD_SDIO_RETRIES_EXCEEDED = 1051
};

enum
{
    WICED_FALSE = 0,
    WICED_TRUE = 1
};

enum
{
    WWD_STA_INTERFACE = 0,
    WWD_AP_INTERFACE = 1
};

enum
{
    WICED_SCAN_TYPE_ACTIVE = 0,
    WICED_BSS_TYPE_ANY = 2,
    WICED_SCAN_INCOMPLETE = 0,
    WICED_SCAN_COMPLETED_SUCCESSFULLY = 1,
    WICED_SCAN_ABORTED = 2
};

#define WICED_COUNTRY_CHINA ((uint32_t)'C' | ((uint32_t)'N' << 8))

wwd_result_t wwd_management_init(uint32_t country, void *buffer_interface_argument);
wwd_result_t wwd_management_wifi_off(void);
wwd_result_t wwd_wifi_scan(int32_t scan_type,
                           int32_t bss_type,
                           const wiced_ssid_t *optional_ssid,
                           const wiced_mac_t *optional_mac,
                           const uint16_t *optional_channel_list,
                           const void *optional_extended_parameters,
                           wiced_scan_result_callback_t callback,
                           wiced_scan_result_t **result,
                           void *user_data,
                           int32_t interface);
wwd_result_t wwd_wifi_get_mac_address(wiced_mac_t *mac, int32_t interface);

void wwd_thread_notify_irq(void);

#endif
