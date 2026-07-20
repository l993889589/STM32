/**
 * @file app_health.h
 * @brief Application service bits used by the BSP watchdog supervisor.
 */

#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include <stdint.h>

#define APP_HEALTH_SERVICE_COMM        (1UL << 0)
#define APP_HEALTH_SERVICE_MONITOR     (1UL << 1)
#define APP_HEALTH_SERVICE_SENSOR      (1UL << 2)
#define APP_HEALTH_SERVICE_USB_RX      (1UL << 3)
#define APP_HEALTH_SERVICE_MODBUS      (1UL << 4)
#define APP_HEALTH_SERVICE_DWIN_RX     (1UL << 5)
#define APP_HEALTH_SERVICE_DWIN_TX     (1UL << 6)
#define APP_HEALTH_SERVICE_PARAM_STORE (1UL << 7)

#define APP_HEALTH_REQUIRED_SERVICES                                      \
    (APP_HEALTH_SERVICE_COMM | APP_HEALTH_SERVICE_MONITOR |               \
     APP_HEALTH_SERVICE_SENSOR | APP_HEALTH_SERVICE_USB_RX |              \
     APP_HEALTH_SERVICE_MODBUS | APP_HEALTH_SERVICE_DWIN_RX |             \
     APP_HEALTH_SERVICE_DWIN_TX | APP_HEALTH_SERVICE_PARAM_STORE)

#endif /* APP_HEALTH_H */
