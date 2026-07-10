/*
 * Bootloader OTA v2 state-machine and version-1 migration entry points.
 *
 * ota_boot_v2_process() is called before the legacy path. Migration functions
 * preserve one v1 manifest copy until the first v2 record commits successfully.
 */
#ifndef OTA_BOOT_V2_H
#define OTA_BOOT_V2_H

#include "ota_boot.h"
#include "ota_layout.h"
#include <stdint.h>

uint8_t ota_boot_v2_record_available(void);
ota_boot_result_t ota_boot_v2_process(void);
uint8_t ota_boot_v2_migrate_confirmed_v1(const ota_manifest_t *manifest);
uint8_t ota_boot_v2_migrate_trial_v1(const ota_manifest_t *manifest);

#endif /* OTA_BOOT_V2_H */
