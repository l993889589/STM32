/*
 * LVGL UI asset store backed by BSP_FLASH external SPI NOR.
 *
 * Purpose:
 *   Maintains two 5 MiB resource slots, selects the newest valid UI image
 *   package at boot, and exposes LVGL image descriptors that stream RGB565
 *   page data directly from external Flash.
 *
 * Usage:
 *   Call ui_asset_store_init() after the GD25 driver is bound and before UI
 *   pages request image sources. Network or USB update services call
 *   ui_asset_update_begin(), sequential ui_asset_update_write(), optional
 *   ui_asset_update_calculate_crc32(), then ui_asset_update_commit().
 *
 * Constraints:
 *   Updates are sequential and write only the inactive slot. The LVGL decoder
 *   keeps reading the active slot during downloads so the current screen does
 *   not blank while a new package is staged. Package metadata, A/B slot base
 *   addresses, and bootloader-reserved external Flash ranges are defined in
 *   ota_layout.h.
 */
#include "ui_asset_store.h"

#include <stddef.h>
#include <string.h>

#include "draw/lv_image_decoder_private.h"
#include "bsp_flash.h"
#include "ota_layout.h"

#define UI_ASSET_MAGIC               0x50414955UL
#define UI_ASSET_SCHEMA_VERSION      1UL
#define UI_ASSET_VALID_FLAG          0xA55A5AA5UL
#define UI_ASSET_STRIP_ROWS          8U
#define UI_ASSET_MAX_IMAGE_COUNT     8U

typedef struct
{
    uint32_t page_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t offset;
    uint32_t size;
    uint32_t stride;
    uint32_t crc32;
} ui_asset_entry_t;

typedef struct
{
    uint32_t base;
    uint32_t version;
    uint32_t total_size;
    uint32_t data_crc32;
    ui_asset_entry_t entries[UI_ASSET_PAGE_COUNT];
    uint8_t valid;
} ui_asset_slot_t;

typedef struct
{
    uint32_t base;
    uint32_t total_size;
    uint32_t version;
    uint32_t received;
    uint32_t sector_offset;
    uint8_t active;
    uint8_t sector_valid;
    uint8_t sector_dirty;
} ui_asset_update_t;

static ui_asset_slot_t s_active_slot;
static ui_asset_update_t s_update;
static lv_image_dsc_t s_page_dsc[UI_ASSET_PAGE_COUNT];
static lv_draw_buf_t s_decoded_buf;
static const uint8_t s_page_data_sentinel[1] = {0U};
static uint8_t s_strip_buf[UI_ASSET_PAGE_WIDTH * UI_ASSET_STRIP_ROWS * 2U];
static uint8_t s_update_header_sector[UI_ASSET_HEADER_SECTOR];
static uint8_t s_update_sector_buf[OTA_EXT_SECTOR_SIZE];
static char s_status[32] = "not initialized";
static char s_update_error[24] = "none";
static uint32_t s_update_error_address;
static uint32_t s_store_generation;
static ui_asset_decoder_stats_t s_decoder_stats;
static uint8_t s_decoder_created;
static volatile uint8_t s_update_busy;

static void set_update_error(const char *error, uint32_t address)
{
    if(error == NULL)
        error = "unknown";

    (void)strncpy(s_update_error, error, sizeof(s_update_error) - 1U);
    s_update_error[sizeof(s_update_error) - 1U] = '\0';
    s_update_error_address = address;
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;

    while(len-- != 0U)
    {
        crc ^= *data++;
        for(uint32_t bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }

    return ~crc;
}

static uint8_t range_in_slot(uint32_t base, uint32_t offset, uint32_t len)
{
    if(len == 0U || offset >= UI_ASSET_SLOT_SIZE || len > UI_ASSET_SLOT_SIZE)
        return 0U;
    if(offset > (UI_ASSET_SLOT_SIZE - len))
        return 0U;
    if(base != UI_ASSET_SLOT_A_ADDR && base != UI_ASSET_SLOT_B_ADDR)
        return 0U;
    return 1U;
}

static uint8_t read_exact(uint32_t address, uint8_t *data, uint32_t len)
{
    while(len != 0U)
    {
        uint32_t chunk = len > 4096U ? 4096U : len;
        if(!bsp_flash_read(address, data, chunk))
            return 0U;
        address += chunk;
        data += chunk;
        len -= chunk;
    }
    return 1U;
}

static uint8_t erase_range(uint32_t base, uint32_t len)
{
    uint32_t end;

    if(!range_in_slot(base, 0U, len))
        return 0U;

    end = (len + OTA_EXT_SECTOR_SIZE - 1U) & ~(OTA_EXT_SECTOR_SIZE - 1U);
    for(uint32_t offset = 0U; offset < end; offset += OTA_EXT_SECTOR_SIZE)
    {
        if(!bsp_flash_erase_4k(base + offset))
            return 0U;
    }
    return 1U;
}

static uint8_t flush_update_sector(void)
{
    uint32_t address;

    if(s_update.sector_valid == 0U || s_update.sector_dirty == 0U)
        return 1U;

    address = s_update.base + s_update.sector_offset;
    for(uint32_t attempt = 0U; attempt < 3U; attempt++)
    {
        if(bsp_flash_erase_4k(address) &&
           bsp_flash_write(address, s_update_sector_buf, sizeof(s_update_sector_buf)) &&
           bsp_flash_read_verify(address, s_update_sector_buf, sizeof(s_update_sector_buf)))
        {
            s_update.sector_dirty = 0U;
            return 1U;
        }
    }

    set_update_error("sector", address);
    return 0U;
}

static uint8_t verify_slot_image_data(const ui_asset_slot_t *slot)
{
    uint32_t data_crc = 0U;

    if(slot == NULL || slot->valid == 0U)
        return 0U;

    for(uint32_t i = 0U; i < UI_ASSET_PAGE_COUNT; i++)
    {
        const ui_asset_entry_t *entry = &slot->entries[i];
        uint32_t remain = entry->size;
        uint32_t address = slot->base + entry->offset;
        uint32_t entry_crc = 0U;

        if(entry->size == 0U)
            return 0U;

        while(remain != 0U)
        {
            uint32_t chunk = remain > sizeof(s_update_header_sector) ?
                             (uint32_t)sizeof(s_update_header_sector) :
                             remain;

            if(!bsp_flash_read(address, s_update_header_sector, chunk))
                return 0U;

            entry_crc = crc32_update(entry_crc, s_update_header_sector, chunk);
            data_crc = crc32_update(data_crc, s_update_header_sector, chunk);
            address += chunk;
            remain -= chunk;
        }

        if(entry_crc != entry->crc32)
        {
            set_update_error("crc image", slot->base + entry->offset);
            return 0U;
        }
    }

    if(data_crc != slot->data_crc32)
    {
        set_update_error("crc data", slot->base);
        return 0U;
    }

    return 1U;
}

static uint8_t parse_slot(uint32_t base, ui_asset_slot_t *slot)
{
    uint8_t header[UI_ASSET_HEADER_SIZE];
    uint8_t table[UI_ASSET_PAGE_COUNT * UI_ASSET_TABLE_ENTRY_SIZE];
    uint32_t image_count;
    uint32_t table_crc;
    uint32_t calc_crc;

    if(!slot || !read_exact(base, header, sizeof(header)))
        return 0U;

    if(get_u32_le(&header[0]) != UI_ASSET_MAGIC ||
       get_u32_le(&header[4]) != UI_ASSET_SCHEMA_VERSION ||
       get_u32_le(&header[8]) != UI_ASSET_HEADER_SIZE ||
       get_u32_le(&header[12]) != UI_ASSET_TABLE_ENTRY_SIZE ||
       get_u32_le(&header[40]) != UI_ASSET_VALID_FLAG)
    {
        return 0U;
    }

    image_count = get_u32_le(&header[16]);
    if(image_count != UI_ASSET_PAGE_COUNT || image_count > UI_ASSET_MAX_IMAGE_COUNT)
        return 0U;

    slot->base = base;
    slot->version = get_u32_le(&header[24]);
    slot->total_size = get_u32_le(&header[20]);
    slot->data_crc32 = get_u32_le(&header[52]);
    if(slot->total_size == 0U || slot->total_size > UI_ASSET_SLOT_SIZE)
        return 0U;

    if(!read_exact(base + get_u32_le(&header[44]), table, sizeof(table)))
        return 0U;

    table_crc = get_u32_le(&header[48]);
    calc_crc = crc32_update(0U, table, sizeof(table));
    if(calc_crc != table_crc)
        return 0U;

    memset(slot->entries, 0, sizeof(slot->entries));
    for(uint32_t i = 0U; i < UI_ASSET_PAGE_COUNT; i++)
    {
        const uint8_t *raw = &table[i * UI_ASSET_TABLE_ENTRY_SIZE];
        ui_asset_entry_t entry;

        entry.page_id = get_u32_le(&raw[0]);
        entry.format = get_u32_le(&raw[4]);
        entry.width = get_u32_le(&raw[8]);
        entry.height = get_u32_le(&raw[12]);
        entry.offset = get_u32_le(&raw[16]);
        entry.size = get_u32_le(&raw[20]);
        entry.stride = get_u32_le(&raw[24]);
        entry.crc32 = get_u32_le(&raw[28]);

        if(entry.page_id >= UI_ASSET_PAGE_COUNT ||
           entry.format != UI_ASSET_FORMAT_RGB565 ||
           entry.width != UI_ASSET_PAGE_WIDTH ||
           entry.height != UI_ASSET_PAGE_HEIGHT ||
           entry.stride != (UI_ASSET_PAGE_WIDTH * 2U) ||
           entry.size != (UI_ASSET_PAGE_WIDTH * UI_ASSET_PAGE_HEIGHT * 2U) ||
           !range_in_slot(base, entry.offset, entry.size))
        {
            return 0U;
        }

        slot->entries[entry.page_id] = entry;
    }

    slot->valid = 1U;
    return 1U;
}

static const ui_asset_entry_t *find_entry_from_src(const void *src)
{
    for(uint32_t i = 0U; i < UI_ASSET_PAGE_COUNT; i++)
    {
        if(src == &s_page_dsc[i] && s_active_slot.entries[i].size != 0U)
            return &s_active_slot.entries[i];
    }
    return NULL;
}

static lv_result_t asset_decoder_info(lv_image_decoder_t *decoder,
                                      lv_image_decoder_dsc_t *dsc,
                                      lv_image_header_t *header)
{
    const ui_asset_entry_t *entry;

    (void)decoder;
    if(!dsc || !header || dsc->src_type != LV_IMAGE_SRC_VARIABLE)
        return LV_RESULT_INVALID;
    entry = find_entry_from_src(dsc->src);
    if(entry == NULL)
        return LV_RESULT_INVALID;

    s_decoder_stats.info_hits++;
    header->magic = LV_IMAGE_HEADER_MAGIC;
    header->cf = LV_COLOR_FORMAT_RGB565;
    header->w = entry->width;
    header->h = entry->height;
    header->stride = entry->stride;
    return LV_RESULT_OK;
}

static lv_result_t asset_decoder_open(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc)
{
    (void)decoder;
    if(find_entry_from_src(dsc->src) == NULL)
        return LV_RESULT_INVALID;

    s_decoder_stats.open_hits++;
    dsc->decoded = NULL;
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = UI_ASSET_PAGE_WIDTH;
    dsc->header.h = UI_ASSET_PAGE_HEIGHT;
    dsc->header.stride = UI_ASSET_PAGE_WIDTH * 2U;
    return LV_RESULT_OK;
}

static lv_result_t asset_decoder_get_area(lv_image_decoder_t *decoder,
                                          lv_image_decoder_dsc_t *dsc,
                                          const lv_area_t *full_area,
                                          lv_area_t *decoded_area)
{
    const ui_asset_entry_t *entry;
    int32_t y1;
    int32_t y2;
    int32_t width;
    int32_t rows;
    uint8_t *dst;

    (void)decoder;
    if(!dsc || !full_area || !decoded_area)
        return LV_RESULT_INVALID;
    entry = find_entry_from_src(dsc->src);
    if(entry == NULL)
        return LV_RESULT_INVALID;

    if(decoded_area->y1 == LV_COORD_MIN)
        y1 = full_area->y1;
    else
        y1 = decoded_area->y2 + 1;

    if(y1 > full_area->y2)
        return LV_RESULT_INVALID;

    y2 = y1 + (int32_t)UI_ASSET_STRIP_ROWS - 1;
    if(y2 > full_area->y2)
        y2 = full_area->y2;

    width = lv_area_get_width(full_area);
    rows = y2 - y1 + 1;
    if(width <= 0 || rows <= 0 ||
       (uint32_t)width > UI_ASSET_PAGE_WIDTH ||
       (uint32_t)rows > UI_ASSET_STRIP_ROWS)
    {
        return LV_RESULT_INVALID;
    }

    dst = s_strip_buf;
    for(int32_t y = y1; y <= y2; y++)
    {
        uint32_t src_offset = entry->offset +
                              ((uint32_t)y * entry->stride) +
                              ((uint32_t)full_area->x1 * 2U);
        uint32_t row_bytes = (uint32_t)width * 2U;
        if(!read_exact(s_active_slot.base + src_offset, dst, row_bytes))
        {
            s_decoder_stats.read_failures++;
            return LV_RESULT_INVALID;
        }
        dst += row_bytes;
    }

    s_decoder_stats.area_hits++;
    decoded_area->x1 = full_area->x1;
    decoded_area->x2 = full_area->x2;
    decoded_area->y1 = y1;
    decoded_area->y2 = y2;

    memset(&s_decoded_buf, 0, sizeof(s_decoded_buf));
    s_decoded_buf.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_decoded_buf.header.cf = LV_COLOR_FORMAT_RGB565;
    s_decoded_buf.header.w = (uint32_t)width;
    s_decoded_buf.header.h = (uint32_t)rows;
    s_decoded_buf.header.stride = (uint32_t)width * 2U;
    s_decoded_buf.data_size = s_decoded_buf.header.stride * (uint32_t)rows;
    s_decoded_buf.data = s_strip_buf;
    dsc->decoded = &s_decoded_buf;
    return LV_RESULT_OK;
}

static void asset_decoder_close(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc)
{
    (void)decoder;
    (void)dsc;
}

static void create_decoder_once(void)
{
    lv_image_decoder_t *decoder;

    if(s_decoder_created != 0U)
        return;

    decoder = lv_image_decoder_create();
    if(decoder != NULL)
    {
        lv_image_decoder_set_info_cb(decoder, asset_decoder_info);
        lv_image_decoder_set_open_cb(decoder, asset_decoder_open);
        lv_image_decoder_set_get_area_cb(decoder, asset_decoder_get_area);
        lv_image_decoder_set_close_cb(decoder, asset_decoder_close);
        decoder->name = "ui_asset";
        s_decoder_created = 1U;
    }
}

bool ui_asset_store_init(void)
{
    ui_asset_slot_t slot_a;
    ui_asset_slot_t slot_b;
    uint8_t ok_a;
    uint8_t ok_b;

    create_decoder_once();

    for(uint32_t i = 0U; i < UI_ASSET_PAGE_COUNT; i++)
    {
        /* Descriptors keep stable addresses; UI code uses generation to drop
           stale LVGL cache entries when the active slot changes. */
        memset(&s_page_dsc[i], 0, sizeof(s_page_dsc[i]));
        s_page_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_page_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_page_dsc[i].header.w = UI_ASSET_PAGE_WIDTH;
        s_page_dsc[i].header.h = UI_ASSET_PAGE_HEIGHT;
        s_page_dsc[i].header.stride = UI_ASSET_PAGE_WIDTH * 2U;
        /* LVGL rejects LV_IMAGE_SRC_VARIABLE before trying custom decoders
           when data is NULL. Pixel data still comes from GD25 via
           asset_decoder_get_area(); this byte only lets the descriptor pass
           LVGL's source pre-check. */
        s_page_dsc[i].data = s_page_data_sentinel;
        s_page_dsc[i].data_size = UI_ASSET_PAGE_WIDTH * UI_ASSET_PAGE_HEIGHT * 2U;
    }

    memset(&slot_a, 0, sizeof(slot_a));
    memset(&slot_b, 0, sizeof(slot_b));
    ok_a = parse_slot(UI_ASSET_SLOT_A_ADDR, &slot_a);
    ok_b = parse_slot(UI_ASSET_SLOT_B_ADDR, &slot_b);

    if(ok_a && ok_b)
        s_active_slot = (slot_b.version >= slot_a.version) ? slot_b : slot_a;
    else if(ok_a)
        s_active_slot = slot_a;
    else if(ok_b)
        s_active_slot = slot_b;
    else
    {
        memset(&s_active_slot, 0, sizeof(s_active_slot));
        (void)strncpy(s_status, "no valid asset", sizeof(s_status) - 1U);
        s_status[sizeof(s_status) - 1U] = '\0';
        s_store_generation++;
        return false;
    }

    (void)strncpy(s_status,
                  s_active_slot.base == UI_ASSET_SLOT_A_ADDR ? "asset slot A" : "asset slot B",
                  sizeof(s_status) - 1U);
    s_status[sizeof(s_status) - 1U] = '\0';
    s_store_generation++;
    return true;
}

bool ui_asset_store_available(void)
{
    return s_active_slot.valid != 0U;
}

uint32_t ui_asset_store_active_version(void)
{
    return s_active_slot.version;
}

uint8_t ui_asset_store_active_slot(void)
{
    if(s_active_slot.valid == 0U)
        return 0xFFU;
    return s_active_slot.base == UI_ASSET_SLOT_A_ADDR ? 0U : 1U;
}

const char *ui_asset_store_status(void)
{
    return s_status;
}

uint32_t ui_asset_store_generation(void)
{
    return s_store_generation;
}

void ui_asset_store_decoder_stats(ui_asset_decoder_stats_t *stats)
{
    if(stats != NULL)
        *stats = s_decoder_stats;
}

const lv_image_dsc_t *ui_asset_store_page_src(uint32_t page_id)
{
    if(page_id >= UI_ASSET_PAGE_COUNT || !ui_asset_store_available())
        return NULL;
    if(s_active_slot.entries[page_id].size == 0U)
        return NULL;
    return &s_page_dsc[page_id];
}

bool ui_asset_update_begin(uint32_t total_size, uint32_t asset_version)
{
    uint32_t target_base;

    if(total_size == 0U || total_size > UI_ASSET_SLOT_SIZE)
    {
        set_update_error("bad size", total_size);
        return false;
    }
    if(asset_version == 0U)
    {
        set_update_error("bad version", asset_version);
        return false;
    }
    if(s_active_slot.valid != 0U && asset_version <= s_active_slot.version)
    {
        set_update_error("old version", asset_version);
        return false;
    }

    if(s_active_slot.valid == 0U || s_active_slot.base == UI_ASSET_SLOT_B_ADDR)
        target_base = UI_ASSET_SLOT_A_ADDR;
    else
        target_base = UI_ASSET_SLOT_B_ADDR;

    s_update_busy = 1U;
    set_update_error("none", 0U);
    if(!erase_range(target_base, total_size))
    {
        set_update_error("erase", target_base);
        s_update_busy = 0U;
        return false;
    }

    memset(&s_update, 0, sizeof(s_update));
    memset(s_update_header_sector, 0xFF, sizeof(s_update_header_sector));
    memset(s_update_sector_buf, 0xFF, sizeof(s_update_sector_buf));
    s_update.base = target_base;
    s_update.total_size = total_size;
    s_update.version = asset_version;
    s_update.received = 0U;
    s_update.active = 1U;
    return true;
}

bool ui_asset_update_write(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t header_copy;
    uint32_t end_offset;

    if(s_update.active == 0U || !data || len == 0U)
    {
        set_update_error("inactive", offset);
        return false;
    }
    if(!range_in_slot(s_update.base, offset, len) || (offset + len) > s_update.total_size)
    {
        set_update_error("range", offset);
        return false;
    }
    if(offset != s_update.received)
    {
        set_update_error("sequence", offset);
        return false;
    }
    end_offset = offset + len;

    if(offset < UI_ASSET_HEADER_SECTOR)
    {
        header_copy = UI_ASSET_HEADER_SECTOR - offset;
        if(header_copy > len)
            header_copy = len;
        memcpy(&s_update_header_sector[offset], data, header_copy);
        offset += header_copy;
        data += header_copy;
        len -= header_copy;
    }

    if(len == 0U)
    {
        if(end_offset > s_update.received)
            s_update.received = end_offset;
        return true;
    }

    while(len != 0U)
    {
        uint32_t sector_offset = offset & ~(OTA_EXT_SECTOR_SIZE - 1U);
        uint32_t in_sector = offset - sector_offset;
        uint32_t chunk = OTA_EXT_SECTOR_SIZE - in_sector;

        if(chunk > len)
            chunk = len;

        if(s_update.sector_valid == 0U || s_update.sector_offset != sector_offset)
        {
            if(!flush_update_sector())
            {
                s_update.active = 0U;
                s_update_busy = 0U;
                return false;
            }

            memset(s_update_sector_buf, 0xFF, sizeof(s_update_sector_buf));
            s_update.sector_offset = sector_offset;
            s_update.sector_valid = 1U;
            s_update.sector_dirty = 0U;
        }

        memcpy(&s_update_sector_buf[in_sector], data, chunk);
        s_update.sector_dirty = 1U;

        offset += chunk;
        data += chunk;
        len -= chunk;
    }

    if(end_offset > s_update.received)
        s_update.received = end_offset;
    return true;
}

bool ui_asset_update_calculate_crc32(uint32_t *crc32)
{
    uint32_t crc = 0U;
    uint32_t offset = 0U;

    if(crc32 == NULL || s_update.active == 0U || s_update.received == 0U)
        return false;

    if(!flush_update_sector())
    {
        s_update.active = 0U;
        s_update_busy = 0U;
        return false;
    }

    while(offset < s_update.received)
    {
        uint32_t remain = s_update.received - offset;
        uint32_t chunk;

        if(offset < UI_ASSET_HEADER_SECTOR)
        {
            chunk = UI_ASSET_HEADER_SECTOR - offset;
            if(chunk > remain)
                chunk = remain;
            crc = crc32_update(crc, &s_update_header_sector[offset], chunk);
        }
        else
        {
            chunk = remain > sizeof(s_update_sector_buf) ?
                    (uint32_t)sizeof(s_update_sector_buf) :
                    remain;
            if(!read_exact(s_update.base + offset, s_update_sector_buf, chunk))
            {
                set_update_error("read crc", s_update.base + offset);
                return false;
            }
            crc = crc32_update(crc, s_update_sector_buf, chunk);
        }

        offset += chunk;
    }

    *crc32 = crc;
    return true;
}

bool ui_asset_update_commit(void)
{
    ui_asset_slot_t test_slot;

    if(s_update.active == 0U)
    {
        set_update_error("inactive commit", 0U);
        return false;
    }
    if(s_update.received < s_update.total_size)
    {
        set_update_error("short commit", s_update.received);
        return false;
    }

    if(!flush_update_sector())
    {
        s_update.active = 0U;
        s_update_busy = 0U;
        return false;
    }

    if(!bsp_flash_erase_4k(s_update.base) ||
       !bsp_flash_write(s_update.base, s_update_header_sector, sizeof(s_update_header_sector)) ||
       !bsp_flash_read_verify(s_update.base, s_update_header_sector, sizeof(s_update_header_sector)))
    {
        set_update_error("header", s_update.base);
        s_update.active = 0U;
        s_update_busy = 0U;
        return false;
    }

    memset(&test_slot, 0, sizeof(test_slot));
    if(!parse_slot(s_update.base, &test_slot))
    {
        set_update_error("parse", s_update.base);
        s_update.active = 0U;
        s_update_busy = 0U;
        return false;
    }

    if(!verify_slot_image_data(&test_slot))
    {
        (void)bsp_flash_erase_4k(s_update.base);
        (void)strncpy(s_status, "asset crc fail", sizeof(s_status) - 1U);
        s_update.active = 0U;
        s_update_busy = 0U;
        return false;
    }

    s_update.active = 0U;
    s_update_busy = 0U;
    return ui_asset_store_init();
}

uint32_t ui_asset_update_received(void)
{
    return s_update.received;
}

uint32_t ui_asset_update_expected(void)
{
    return s_update.total_size;
}

const char *ui_asset_update_error(void)
{
    return s_update_error;
}

uint32_t ui_asset_update_error_address(void)
{
    return s_update_error_address;
}
