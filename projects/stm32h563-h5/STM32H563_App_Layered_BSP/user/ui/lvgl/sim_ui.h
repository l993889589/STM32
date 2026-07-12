/**
 * @file sim_ui.h
 * @brief Multi-page LVGL presentation including the dual-CAN self-test page.
 */

#ifndef SIM_UI_H
#define SIM_UI_H

#include "lvgl.h"

typedef enum
{
    SIM_UI_PAGE_DASHBOARD = 0,
    SIM_UI_PAGE_COMM = 1,
    SIM_UI_PAGE_CAN_SELF_TEST = 2,
    SIM_UI_PAGE_BOARD_SELF_TEST = 3
} sim_ui_page_t;

/** @brief Initialize the full multi-page UI on the active screen. */
void sim_ui_init(void);

/** @brief Refresh dynamic data after the application model is updated. */
void sim_ui_update(void);

/** @brief Select a logical dashboard, communication, CAN, or board-test page. */
int sim_ui_set_page(sim_ui_page_t page);

/** @brief Return the current logical page selection. */
sim_ui_page_t sim_ui_get_page(void);

#endif /* SIM_UI_H */
