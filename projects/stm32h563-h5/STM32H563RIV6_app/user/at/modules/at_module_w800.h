/**
 * @file at_module_w800.h
 * @brief W800-specific AT operations layered on the generic module facade.
 */

#ifndef AT_MODULE_W800_H
#define AT_MODULE_W800_H

#include "at_module.h"

extern const at_module_driver_t g_at_module_w800;

#define AT_W800_SCAN_MAX_RESULTS 8U

typedef struct
{
    char ssid[33];
    int16_t rssi_dbm;
    uint8_t channel;
    uint8_t encryption;
} at_w800_access_point_t;

/**
 * @brief Start the W800 built-in BLE Wi-Fi provisioning service.
 * @param module Initialized W800 AT module owning the USART1 session.
 * @return True when the module accepts AT+ONESHOT=4; otherwise false.
 * @note Call from the W800 task only. The service exposes GATT service 0x1824
 *       and write/indicate characteristic 0x2ABC.
 */
bool at_module_w800_start_ble_provision(at_module_t *module);

/**
 * @brief Stop any active W800 one-shot provisioning service.
 * @param module Initialized W800 AT module owning the USART1 session.
 * @return True when the module accepts AT+ONESHOT=0; otherwise false.
 */
bool at_module_w800_stop_provision(at_module_t *module);

/**
 * @brief Save one WPA/WPA2 station profile supplied through the USB rescue path.
 * @param module Initialized W800 AT module owning the USART1 session.
 * @param ssid Printable ASCII SSID, 1..32 bytes, excluding a double quote.
 * @param password Printable ASCII WPA/WPA2 password, 8..63 bytes, excluding a double quote.
 * @return True only after every setting and AT+PMTF succeeds.
 * @note The caller must reset the W800 after success. This function never logs
 *       or retains either credential.
 */
bool at_module_w800_save_station_profile(at_module_t *module,
                                         const char *ssid,
                                         const char *password);

/** @brief Scan nearby access points and return strongest-first unique SSIDs. */
bool at_module_w800_scan_access_points(at_module_t *module,
                                       at_w800_access_point_t *access_points,
                                       uint8_t capacity,
                                       uint8_t *count);

#endif /* AT_MODULE_W800_H */
