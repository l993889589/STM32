#include "lvgl.h"
#include "sim_ui.h"
#include "sim_data.h"
#include <windows.h>
#include <stdio.h>

#if LV_USE_WINDOWS
#include "src/drivers/windows/lv_windows_display.h"
#include "src/drivers/windows/lv_windows_input.h"
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /* Initialize LVGL core */
    lv_init();

    /* Create display: 480x320, zoom 200% for a 960x640 window */
    lv_display_t *disp = lv_windows_create_display(
        L"LeduO W800 Simulator",
        480, 320,
        200,       /* zoom level: 200% */
        true,      /* allow DPI override */
        true       /* simulator mode */
    );

    if(disp == NULL) {
        MessageBoxW(NULL, L"Failed to create LVGL display",
                    L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    /* Acquire pointer (mouse) input device */
    lv_windows_acquire_pointer_indev(disp);

    /* Initialize mock data and build UI */
    sim_data_init();
    sim_ui_init();

    /* Main loop */
    uint32_t tick_counter = 0;
    MSG msg;
    bool running = true;

    while(running) {
        /* Pump Windows messages */
        while(PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if(msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        /* LVGL timer handler */
        lv_timer_handler();

        /* Data refresh every ~500ms (500 * 1ms Sleep) */
        tick_counter++;
        if(tick_counter >= 500) {
            tick_counter = 0;
            sim_data_tick();
            sim_ui_update();
        }

        Sleep(1);
    }

    return 0;
}
