/*
 * LVGL simulator adapter for the external-Flash UI asset store.
 *
 * The target implementation reads the active A/B package from GD25LQ128.
 * A desktop simulator has no board Flash, so this file presents the five
 * generated reference images through the same read-only API used by sim_ui.c.
 * Keep this adapter host-only; it must never be added to the STM32 project.
 */

#include "ui_asset_store.h"

#include "ref_page_img.h"

bool ui_asset_store_available(void)
{
    return true;
}

uint32_t ui_asset_store_active_version(void)
{
    return 1U;
}

const char *ui_asset_store_status(void)
{
    return "sim";
}

const lv_image_dsc_t *ui_asset_store_page_src(uint32_t page_id)
{
    if(page_id >= REF_PAGE_COUNT)
        return NULL;

    return ref_pages[page_id];
}

uint32_t ui_asset_store_generation(void)
{
    return 1U;
}
