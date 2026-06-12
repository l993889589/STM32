#ifndef APP_LDC_CONFIG_H
#define APP_LDC_CONFIG_H

#include <stdint.h>

#include "ldc_frame_policy.h"

typedef enum
{
    APP_LDC_PORT_USB_CDC = 0,
    APP_LDC_PORT_RS485,
    APP_LDC_PORT_W800_AT,
    APP_LDC_PORT_COUNT
} app_ldc_port_id_t;

typedef enum
{
    APP_LDC_LINK_USB_CDC = 0,
    APP_LDC_LINK_UART
} app_ldc_link_type_t;

typedef struct
{
    app_ldc_port_id_t id;
    const char *name;
    app_ldc_link_type_t link_type;
    uint32_t baudrate;
    uint32_t max_frame;
    ldc_frame_policy_t policy;
} app_ldc_port_config_t;

const app_ldc_port_config_t *app_ldc_config_get(app_ldc_port_id_t id);
void app_ldc_config_apply(ldc_t *ldc, app_ldc_port_id_t id);

#endif /* APP_LDC_CONFIG_H */
