#ifndef APP_AP6212_BRIDGE_H
#define APP_AP6212_BRIDGE_H

#include "app_serial_ldc.h"
#include "tx_api.h"

UINT app_ap6212_bridge_init(void);
app_serial_ldc_t *app_ap6212_bridge_debug_serial(void);
app_serial_ldc_t *app_ap6212_bridge_bt_serial(void);

#endif /* APP_AP6212_BRIDGE_H */
