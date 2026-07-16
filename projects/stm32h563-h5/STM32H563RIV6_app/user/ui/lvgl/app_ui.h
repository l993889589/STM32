/**
 * @file app_ui.h
 * @brief ThreadX LVGL application lifecycle and logical page selection.
 */

#ifndef APP_UI_H
#define APP_UI_H

#include "tx_api.h"

typedef enum
{
    APP_UI_PAGE_DASHBOARD = 0,
    APP_UI_PAGE_COMM = 1,
    APP_UI_PAGE_CAN_SELF_TEST = 2,
    APP_UI_PAGE_BOARD_SELF_TEST = 3
} app_ui_page_t;

/** @brief Initialize LVGL, display/input ports, assets, and all pages. */
UINT app_ui_init(void);
/** @brief Run the periodic LVGL task owned by ThreadX. */
void app_ui_task_entry(ULONG thread_input);
/** @brief Select one logical page without deleting any existing page. */
int app_ui_request_page(app_ui_page_t page);
/** @brief Return the current logical page selection. */
app_ui_page_t app_ui_get_page(void);

#endif /* APP_UI_H */
