/*
 * Boot-only firmware authenticity adapter.
 *
 * It re-hashes the immutable external slot, applies anti-rollback policy and
 * verifies raw ECDSA P-256 r||s with the software micro-ecc backend.
 */
#ifndef BOOT_SECURITY_H
#define BOOT_SECURITY_H

#include "ota_boot_control.h"
#include <stdint.h>

#define OTA_BOOT_COMPILED_MINIMUM_VERSION 2026071000UL

#define BOOT_SECURITY_BACKEND_MICRO_ECC 1U

typedef struct
{
    uint32_t backend;
    uint32_t verify_status;
    uint8_t signature_valid;
    uint8_t attempted;
    uint8_t digest[32];
} boot_security_diagnostics_t;

/* Authenticate one external slot and map a failure to persistent OTA error. */
uint8_t boot_security_verify_slot(
    const ota_boot_control_record_t *record,
    uint32_t slot,
    uint32_t *control_error);

/* Returns the last signature-verification evidence for the maintenance shell. */
void boot_security_get_diagnostics(boot_security_diagnostics_t *diagnostics);

#endif /* BOOT_SECURITY_H */
