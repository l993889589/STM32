#include "lv_port_indev.h"

#include "bsp_touch.h"

static lv_indev_t *s_touch_indev;
static lv_point_t s_last_point;

static void lv_port_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    bsp_touch_state_t touch;

    (void)indev;

    data->point = s_last_point;
    data->state = LV_INDEV_STATE_RELEASED;

    if(bsp_touch_read(&touch) != 0)
    {
        return;
    }

    if(touch.touched != 0U)
    {
        s_last_point.x = (lv_coord_t)touch.x;
        s_last_point.y = (lv_coord_t)touch.y;
        data->point = s_last_point;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

lv_indev_t *lv_port_indev_init(void)
{
    if(s_touch_indev != NULL)
    {
        return s_touch_indev;
    }

    (void)bsp_touch_init();

    s_touch_indev = lv_indev_create();
    if(s_touch_indev == NULL)
    {
        return NULL;
    }

    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lv_port_touch_read);

    return s_touch_indev;
}
