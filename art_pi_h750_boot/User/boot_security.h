#ifndef BOOT_SECURITY_H
#define BOOT_SECURITY_H

#include <stdint.h>

#include "gateway_ota_format.h"

uint8_t boot_security_hash_spi(uint32_t address,
                               uint32_t size,
                               uint8_t digest[32]);
uint8_t boot_security_hash_qspi(uint32_t address,
                                uint32_t size,
                                uint8_t digest[32]);
uint8_t boot_security_verify_manifest_signature(
    const gateway_ota_manifest_t *manifest);

#endif
