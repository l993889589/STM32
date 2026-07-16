#ifndef DRV_DWIN_H
#define DRV_DWIN_H

#include <stdint.h>

#include "bsp_status.h"

bsp_status_t drv_dwin_init(void);
bsp_status_t drv_dwin_write(const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms);

#endif
