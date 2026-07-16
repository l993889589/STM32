/**
 * @file app_ui.c
 * @brief ThreadX owner for LVGL initialization, refresh, and page requests.
 */

#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_ui_model.h"
#include "../../app/app_health.h"
#include "bsp_irq_lock.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "sim_ui.h"
#include "ui_asset_store.h"

#define APP_UI_TICK_MS             5UL
#define APP_UI_REFRESH_PERIOD_MS   500UL

static uint8_t s_ui_initialized;
static volatile app_ui_page_t s_current_page = APP_UI_PAGE_DASHBOARD;
static volatile app_ui_page_t s_requested_page = APP_UI_PAGE_DASHBOARD;
static volatile uint8_t s_page_request_pending;

/** @brief Initialize LVGL and construct the complete multi-page interface. */
UINT app_ui_init(void)
{
    if(s_ui_initialized != 0U)
    {
        return TX_SUCCESS;
    }

    lv_init();

    if(lv_port_disp_init() == NULL)
    {
        return TX_PTR_ERROR;
    }
    if(lv_port_indev_init() == NULL)
    {
        return TX_PTR_ERROR;
    }

    /* Build the product UI from real application diagnostics. */
    (void)ui_asset_store_init();
    sim_ui_init();

    s_ui_initialized = 1U;
    return TX_SUCCESS;
}

/** @brief Map one application page request to the LVGL presentation. */
int app_ui_request_page(app_ui_page_t page)
{
    bsp_irq_state_t irq_state;

    if(page > APP_UI_PAGE_BOARD_SELF_TEST)
    {
        return -1;
    }
    irq_state = bsp_irq_lock();
    s_requested_page = page;
    s_page_request_pending = 1U;
    bsp_irq_unlock(irq_state);
    return 0;
}

/** @brief Return the current logical page reported by the presentation. */
app_ui_page_t app_ui_get_page(void)
{
    bsp_irq_state_t irq_state = bsp_irq_lock();
    app_ui_page_t page = s_current_page;

    bsp_irq_unlock(irq_state);
    return page;
}

/** @brief Run LVGL ticks, model refresh, and timer handling forever. */
void app_ui_task_entry(ULONG thread_input)
{
    ULONG elapsed_ms = 0UL;
    UINT init_status;

    TX_PARAMETER_NOT_USED(thread_input);

    init_status = app_ui_init();
    if(init_status != TX_SUCCESS)
    {
        app_health_report_fault(APP_HEALTH_FAULT_UI_INIT);
        for(;;)
        {
            tx_thread_sleep(1000U);
        }
    }

    for(;;)
    {
        app_ui_page_t requested_page = APP_UI_PAGE_DASHBOARD;
        uint8_t has_request;
        bsp_irq_state_t irq_state;

        app_health_report(APP_HEALTH_SERVICE_UI);
        lv_tick_inc(APP_UI_TICK_MS);

        irq_state = bsp_irq_lock();
        has_request = s_page_request_pending;
        if(has_request != 0U)
        {
            requested_page = s_requested_page;
            s_page_request_pending = 0U;
        }
        bsp_irq_unlock(irq_state);
        if(has_request != 0U)
        {
            (void)sim_ui_set_page((sim_ui_page_t)requested_page);
        }
        s_current_page = (app_ui_page_t)sim_ui_get_page();

        elapsed_ms += APP_UI_TICK_MS;
        if(elapsed_ms >= APP_UI_REFRESH_PERIOD_MS)
        {
            elapsed_ms = 0UL;
            sim_ui_update();
        }

        (void)lv_timer_handler();
        tx_thread_sleep(APP_UI_TICK_MS);
    }
}
