/*
 * Bootloader-private internal/external image operations used by OTA v2.
 *
 * These functions execute only in boot context. They verify source data before
 * erasing internal App flash and verify the complete programmed image afterward.
 */
#ifndef OTA_BOOT_PRIVATE_H
#define OTA_BOOT_PRIVATE_H

#include <stdint.h>

uint8_t ota_boot_internal_image_matches(uint32_t image_size, uint32_t image_crc32);
uint8_t ota_boot_external_image_is_valid(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32);
uint8_t ota_boot_install_external_image(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32);
uint32_t ota_boot_reset_reason(void);

#endif /* OTA_BOOT_PRIVATE_H */
