#ifndef REF_PAGE_IMG_H
#define REF_PAGE_IMG_H

#include "lvgl.h"

#define REF_PAGE_WIDTH  480U
#define REF_PAGE_HEIGHT 320U
#define REF_PAGE_COUNT  5U

#define REF_PAGE_BRAND_INDEX   0U
#define REF_PAGE_MONITOR_INDEX 1U
#define REF_PAGE_SENSOR_INDEX  2U
#define REF_PAGE_EVENT_INDEX   3U
#define REF_PAGE_COMM_INDEX    4U

extern const lv_image_dsc_t ref_page_brand;
extern const lv_image_dsc_t ref_page_monitor;
extern const lv_image_dsc_t ref_page_sensor;
extern const lv_image_dsc_t ref_page_event;
extern const lv_image_dsc_t ref_page_comm;
extern const lv_image_dsc_t *const ref_pages[REF_PAGE_COUNT];

#endif /* REF_PAGE_IMG_H */
