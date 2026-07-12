/**
 * @file app_w800_config.h
 * @brief Public W800 network configuration defaults and local override hook.
 *
 * Copy app_w800_config_local.example.h to app_w800_config_local.h and edit
 * only that ignored file for hardware tests. A copied source tree therefore
 * builds safely without carrying credentials or a machine-specific endpoint.
 */

#ifndef APP_W800_CONFIG_H
#define APP_W800_CONFIG_H

#if defined(__has_include)
#if __has_include("app_w800_config_local.h")
#include "app_w800_config_local.h"
#endif
#endif

#ifndef APP_W800_STATUS_MQTT_ENABLE
#define APP_W800_STATUS_MQTT_ENABLE 0U
#endif

#ifndef APP_W800_WIFI_SSID
#define APP_W800_WIFI_SSID ""
#endif

#ifndef APP_W800_WIFI_PASSWORD
#define APP_W800_WIFI_PASSWORD ""
#endif

#ifndef APP_W800_MQTT_HOST
#define APP_W800_MQTT_HOST "0.0.0.0"
#endif

#ifndef APP_W800_MQTT_PORT
#define APP_W800_MQTT_PORT 1883U
#endif

#endif /* APP_W800_CONFIG_H */
