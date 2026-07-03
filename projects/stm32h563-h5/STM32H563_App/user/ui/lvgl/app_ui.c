#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_ui_model.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "sim_ui.h"
#include "sim_data.h"

#define APP_UI_TICK_MS             5UL
#define APP_UI_REFRESH_PERIOD_MS   500UL

UINT app_ui_init(void)
{
    lv_init();

    if(lv_port_disp_init() == NULL)
    {
        return TX_PTR_ERROR;
    }

    /* Initialize mock data and build cyberpunk UI */
    sim_data_init();
    sim_ui_init();

    return TX_SUCCESS;
}

int app_ui_request_page(app_ui_page_t page)
{
    /* Page switching is handled by sim_ui tab buttons */
    (void)page;
    return 0;
}

app_ui_page_t app_ui_get_page(void)
{
    return APP_UI_PAGE_DASHBOARD;
}

void app_ui_task_entry(ULONG thread_input)
{
    ULONG elapsed_ms = 0UL;

    TX_PARAMETER_NOT_USED(thread_input);

    for(;;)
    {
        lv_tick_inc(APP_UI_TICK_MS);

        elapsed_ms += APP_UI_TICK_MS;
        if(elapsed_ms >= APP_UI_REFRESH_PERIOD_MS)
        {
            elapsed_ms = 0UL;
            /* Update mock data and refresh UI */
            sim_data_tick();
            sim_ui_update();
        }

        (void)lv_timer_handler();
        tx_thread_sleep(APP_UI_TICK_MS);
    }
}
