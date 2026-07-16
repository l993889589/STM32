/**
 * @file app_w800_config.h
 * @brief Public W800 MQTT endpoint defaults and local override hook.
 *
 * Copy app_w800_config_local.example.h to app_w800_config_local.h and edit
 * only the non-secret broker endpoint in that ignored file for hardware tests.
 * Wi-Fi credentials are provisioned through W800 BLE or entered transiently by
 * the masked USB rescue flow. They must not be added to this header or any
 * STM32 build input.
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

#ifndef APP_W800_MQTT_HOST
#define APP_W800_MQTT_HOST "0.0.0.0"
#endif

#ifndef APP_W800_MQTT_PORT
#define APP_W800_MQTT_PORT 1883U
#endif

#endif /* APP_W800_CONFIG_H */
