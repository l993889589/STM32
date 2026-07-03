#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_TOUCH_WIDTH   480U
#define BSP_TOUCH_HEIGHT  320U

typedef struct
{
    uint8_t present;
    uint8_t touched;
    uint8_t points;
    uint16_t x;
    uint16_t y;
    uint8_t event;
    uint8_t gesture;
    uint8_t chip_id;
    uint8_t vendor_id;
    uint8_t int_active;
} bsp_touch_state_t;

int bsp_touch_init(void);
int bsp_touch_read(bsp_touch_state_t *state);
uint8_t bsp_touch_int_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TOUCH_H */
