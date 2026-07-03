#include "app_ap6212_fw_bundle.h"

#include <string.h>

#include "app_qspi_loader.h"
#include "bsp_qspi_flash.h"

#define AP6212_FW_BUNDLE_MAGIC0      'A'
#define AP6212_FW_BUNDLE_MAGIC1      'P'
#define AP6212_FW_BUNDLE_MAGIC2      'F'
#define AP6212_FW_BUNDLE_MAGIC3      'W'
#define AP6212_FW_BUNDLE_HEADER_SIZE 64U

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool header_magic_valid(const uint8_t *header)
{
    return header[0] == (uint8_t)AP6212_FW_BUNDLE_MAGIC0 &&
           header[1] == (uint8_t)AP6212_FW_BUNDLE_MAGIC1 &&
           header[2] == (uint8_t)AP6212_FW_BUNDLE_MAGIC2 &&
           header[3] == (uint8_t)AP6212_FW_BUNDLE_MAGIC3;
}

static bool bundle_range_valid(uint32_t offset, uint32_t length)
{
    return length != 0U &&
           offset < APP_QSPI_LOADER_SIZE &&
           length <= (APP_QSPI_LOADER_SIZE - offset);
}

int app_ap6212_fw_bundle_read_info(app_ap6212_fw_bundle_info_t *info)
{
    uint8_t header[AP6212_FW_BUNDLE_HEADER_SIZE];

    if(info == NULL)
        return -1;

    memset(info, 0, sizeof(*info));
    if(bsp_qspi_flash_read(APP_QSPI_LOADER_BASE, header, sizeof(header)) != 0)
        return -2;

    if(!header_magic_valid(header))
        return -3;

    info->version = get_u32_le(&header[4]);
    info->total_size = get_u32_le(&header[8]);
    info->header_size = get_u32_le(&header[12]);
    info->firmware_offset = info->header_size;
    info->firmware_length = get_u32_le(&header[16]);
    info->nvram_offset = get_u32_le(&header[20]);
    info->nvram_length = get_u32_le(&header[24]);
    info->firmware_crc32 = get_u32_le(&header[28]);
    info->nvram_crc32 = get_u32_le(&header[32]);
    info->bundle_crc32 = get_u32_le(&header[36]);

    if(info->version == 0U ||
       info->header_size < AP6212_FW_BUNDLE_HEADER_SIZE ||
       info->header_size > APP_QSPI_LOADER_SIZE ||
       !bundle_range_valid(info->firmware_offset, info->firmware_length) ||
       !bundle_range_valid(info->nvram_offset, info->nvram_length) ||
       info->total_size > APP_QSPI_LOADER_SIZE ||
       info->firmware_offset + info->firmware_length > info->total_size ||
       info->nvram_offset + info->nvram_length > info->total_size)
    {
        return -4;
    }

    info->firmware_crc32_readback =
        bsp_qspi_flash_crc32(APP_QSPI_LOADER_BASE + info->firmware_offset,
                             info->firmware_length);
    info->nvram_crc32_readback =
        bsp_qspi_flash_crc32(APP_QSPI_LOADER_BASE + info->nvram_offset,
                             info->nvram_length);

    return 0;
}

bool app_ap6212_fw_bundle_crc_ok(const app_ap6212_fw_bundle_info_t *info)
{
    return info != NULL &&
           info->firmware_crc32 == info->firmware_crc32_readback &&
           info->nvram_crc32 == info->nvram_crc32_readback;
}
