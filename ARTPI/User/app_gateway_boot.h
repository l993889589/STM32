#ifndef APP_GATEWAY_BOOT_H
#define APP_GATEWAY_BOOT_H

#include <stdint.h>

void app_gateway_boot_publish_image(uint32_t image_version);
void app_gateway_boot_mark_healthy(void);

#endif
