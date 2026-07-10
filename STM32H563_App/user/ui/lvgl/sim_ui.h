#ifndef SIM_UI_H
#define SIM_UI_H

#include "lvgl.h"

/* Initialize the full multi-page UI on the active screen */
void sim_ui_init(void);

/* Refresh dynamic data (call every ~500ms after sim_data_tick) */
void sim_ui_update(void);

#endif /* SIM_UI_H */
