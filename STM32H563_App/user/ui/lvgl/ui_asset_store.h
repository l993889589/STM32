/*
 * External-Flash LVGL UI asset store.
 *
 * Purpose:
 *   Presents the active UI resource package in GD25LQ128 as LVGL image
 *   descriptors, and provides the write/verify/commit API used by HTTP Range,
 *   MQTT chunk, and USB update paths.
 *
 * Usage:
 *   Initialize once with ui_asset_store_init() after LVGL and the GD25 driver
 *   are ready. Call ui_asset_store_page_src() from the UI thread to bind a page
 *   image. Update writers must call begin -> write in ascending offset order ->
 *   optional calculate_crc32 -> commit. ui_asset_store_generation() changes
 *   whenever the active asset view is rebuilt, so UI code can drop cached image
 *   data and rebind the current page.
 *
 * Constraints:
 *   The package is a fixed 480x320 RGB565 page table. The active slot is only
 *   switched after metadata parsing and whole-package image CRC validation.
 */
#ifndef UI_ASSET_STORE_H
#define UI_ASSET_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define UI_ASSET_PAGE_WIDTH       480U
#define UI_ASSET_PAGE_HEIGHT      320U
#define UI_ASSET_PAGE_COUNT       5U

#define UI_ASSET_PAGE_BRAND       0U
#define UI_ASSET_PAGE_MONITOR     1U
#define UI_ASSET_PAGE_SENSOR      2U
#define UI_ASSET_PAGE_EVENT       3U
#define UI_ASSET_PAGE_COMM        4U

#define UI_ASSET_HEADER_SIZE      256U
#define UI_ASSET_HEADER_SECTOR    4096U
#define UI_ASSET_TABLE_ENTRY_SIZE 32U
#define UI_ASSET_FORMAT_RGB565    1U

typedef struct
{
    uint32_t info_hits;
    uint32_t open_hits;
    uint32_t area_hits;
    uint32_t read_failures;
} ui_asset_decoder_stats_t;

/* Re-scan A/B slots, select the newest valid package, and rebuild page descriptors. */
bool ui_asset_store_init(void);
/* Return true when at least one valid external-Flash UI asset package is active. */
bool ui_asset_store_available(void);
/* Return the active package version from the UIAP header, or 0 when unavailable. */
uint32_t ui_asset_store_active_version(void);
/* Return 0 for slot A, 1 for slot B, or 0xFF when no valid package is active. */
uint8_t ui_asset_store_active_slot(void);
/* Return a short diagnostic string naming the active slot or the latest failure. */
const char *ui_asset_store_status(void);
/* Return the LVGL image source for a page id; NULL means fallback UI should show. */
const lv_image_dsc_t *ui_asset_store_page_src(uint32_t page_id);
/* Return a monotonic counter bumped whenever ui_asset_store_init() rebuilds state. */
uint32_t ui_asset_store_generation(void);
/* Copy LVGL decoder counters for remote diagnostics; safe from the UI/status thread. */
void ui_asset_store_decoder_stats(ui_asset_decoder_stats_t *stats);

/* Start a newer package in the inactive slot; zero/equal/older versions fail. */
bool ui_asset_update_begin(uint32_t total_size, uint32_t asset_version);
/* Append a sequential payload fragment at offset; out-of-order writes are rejected. */
bool ui_asset_update_write(uint32_t offset, const uint8_t *data, uint32_t len);
/* Calculate CRC32 over the bytes written so far, after flushing pending sector data. */
bool ui_asset_update_calculate_crc32(uint32_t *crc32);
/* Validate and activate the completed inactive-slot package. */
bool ui_asset_update_commit(void);
/* Return bytes successfully accepted by the current update session. */
uint32_t ui_asset_update_received(void);
/* Return total expected bytes for the current update session. */
uint32_t ui_asset_update_expected(void);
/* Return the last update error keyword. */
const char *ui_asset_update_error(void);
/* Return the Flash address or offset associated with the last update error. */
uint32_t ui_asset_update_error_address(void);

#endif /* UI_ASSET_STORE_H */
