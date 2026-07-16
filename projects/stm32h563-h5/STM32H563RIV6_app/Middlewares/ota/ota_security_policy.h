/*
 * Transport-independent firmware authenticity and rollback policy.
 *
 * Metadata is encoded canonically before hashing so PC signing tools and Boot
 * verify exactly the same version, addresses, flags, image hash and size.
 */
#ifndef OTA_SECURITY_POLICY_H
#define OTA_SECURITY_POLICY_H

#include "ota_boot_control.h"
#include "ota_sha256.h"
#include <stdint.h>

#define OTA_SECURITY_MANIFEST_SCHEMA 1UL

typedef enum
{
    OTA_SECURITY_OK = 0,
    OTA_SECURITY_INVALID_ARGUMENT = 1,
    OTA_SECURITY_UNSIGNED = 2,
    OTA_SECURITY_BAD_IMAGE_HASH = 3,
    OTA_SECURITY_BAD_SIGNATURE = 4,
    OTA_SECURITY_VERSION_ROLLBACK = 5
} ota_security_status_t;

typedef uint8_t (*ota_security_verify_fn)(
    void *context,
    const uint8_t digest[OTA_SHA256_DIGEST_SIZE],
    const uint8_t signature[64]);

/* Hash the canonical little-endian descriptor fields signed by the release key. */
void ota_security_manifest_digest(
    const ota_firmware_descriptor_t *descriptor,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE]);

/* Enforce signed-image, minimum-version and verifier acceptance policy. */
ota_security_status_t ota_security_check_descriptor(
    const ota_firmware_descriptor_t *descriptor,
    uint32_t minimum_version,
    ota_security_verify_fn verify,
    void *verify_context);

#endif /* OTA_SECURITY_POLICY_H */
