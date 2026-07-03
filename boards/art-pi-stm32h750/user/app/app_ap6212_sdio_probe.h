#ifndef APP_AP6212_SDIO_PROBE_H
#define APP_AP6212_SDIO_PROBE_H

#include <stdint.h>

#include "tx_api.h"

UINT app_ap6212_sdio_probe_init(void);
uint8_t app_ap6212_wifi_is_ready(void);
int app_ap6212_wifi_get_mac(uint8_t mac[6]);
int app_ap6212_wifi_send_ethernet(const uint8_t *frame, uint16_t length);
int app_ap6212_wifi_receive_ethernet(uint8_t *frame,
                                     uint16_t capacity,
                                     uint16_t *length,
                                     uint32_t timeout_ticks);

#endif /* APP_AP6212_SDIO_PROBE_H */
