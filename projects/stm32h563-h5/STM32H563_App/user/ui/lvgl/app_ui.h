#ifndef APP_UI_H
#define APP_UI_H

#include "tx_api.h"

typedef enum
{
    APP_UI_PAGE_DASHBOARD = 0,
    APP_UI_PAGE_COMM = 1
} app_ui_page_t;

UINT app_ui_init(void);
void app_ui_task_entry(ULONG thread_input);
int app_ui_request_page(app_ui_page_t page);
app_ui_page_t app_ui_get_page(void);

#endif /* APP_UI_H */
