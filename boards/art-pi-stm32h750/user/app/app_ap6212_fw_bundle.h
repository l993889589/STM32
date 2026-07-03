#ifndef APP_AP6212_FW_BUNDLE_H
#define APP_AP6212_FW_BUNDLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t version;
    uint32_t total_size;
    uint32_t header_size;
    uint32_t firmware_offset;
    uint32_t firmware_length;
    uint32_t nvram_offset;
    uint32_t nvram_length;
    uint32_t firmware_crc32;
    uint32_t nvram_crc32;
    uint32_t bundle_crc32;
    uint32_t firmware_crc32_readback;
    uint32_t nvram_crc32_readback;
} app_ap6212_fw_bundle_info_t;

int app_ap6212_fw_bundle_read_info(app_ap6212_fw_bundle_info_t *info);
bool app_ap6212_fw_bundle_crc_ok(const app_ap6212_fw_bundle_info_t *info);

#endif /* APP_AP6212_FW_BUNDLE_H */
