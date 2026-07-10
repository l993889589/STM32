/*
 * Boot-only firmware authenticity adapter.
 *
 * It re-hashes the immutable external slot, applies anti-rollback policy and
 * verifies raw ECDSA P-256 r||s with the STM32H563 PKA peripheral.
 */
#ifndef BOOT_SECURITY_H
#define BOOT_SECURITY_H

#include "ota_boot_control.h"
#include <stdint.h>

#define OTA_BOOT_COMPILED_MINIMUM_VERSION 2026071000UL

uint8_t boot_security_verify_slot(
    const ota_boot_control_record_t *record,
    uint32_t slot,
    uint32_t *control_error);

#endif /* BOOT_SECURITY_H */
