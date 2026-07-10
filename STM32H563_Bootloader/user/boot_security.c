/*
 * STM32H563 SHA-256 policy and software ECDSA-P256 verification adapter.
 *
 * STM32H563 does not implement the PKA accelerator exposed by the shared H5
 * device headers. micro-ecc verifies the raw big-endian r||s signature with
 * the same secp256r1 trust anchor used by the host release tooling.
 */
#include "boot_security.h"

#include "boot_trust_anchor.h"
#include "gd25lq128.h"
#include "ota_security_policy.h"
#include "ota_sha256.h"
#include "uECC.h"
#include <string.h>

#define BOOT_SECURITY_P256_SIZE 32U
#define BOOT_SECURITY_READ_SIZE 512U
static boot_security_diagnostics_t boot_security_diagnostics;

static uint32_t boot_security_slot_address(uint32_t slot)
{
    return (slot == (uint32_t)OTA_FIRMWARE_SLOT_A) ?
           OTA_EXT_FIRMWARE_SLOT_A_ADDR : OTA_EXT_FIRMWARE_SLOT_B_ADDR;
}

static uint8_t boot_security_hash_slot(
    uint32_t address,
    uint32_t size,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE])
{
    ota_sha256_context_t sha256;
    uint8_t buffer[BOOT_SECURITY_READ_SIZE];
    uint32_t offset = 0U;

    ota_sha256_init(&sha256);
    while(offset < size)
    {
        uint32_t chunk = size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }
        if(!gd25lq128_read(address + offset, buffer, chunk))
        {
            memset(buffer, 0, sizeof(buffer));
            return 0U;
        }
        ota_sha256_update(&sha256, buffer, chunk);
        offset += chunk;
    }
    ota_sha256_finish(&sha256, digest);
    memset(buffer, 0, sizeof(buffer));
    return 1U;
}

static uint8_t boot_security_ecdsa_verify(
    void *context,
    const uint8_t digest[OTA_SHA256_DIGEST_SIZE],
    const uint8_t signature[64])
{
    uint8_t public_key[BOOT_SECURITY_P256_SIZE * 2U];
    int status;
    uint8_t valid;

    (void)context;
    boot_security_diagnostics.attempted = 1U;
    boot_security_diagnostics.backend = BOOT_SECURITY_BACKEND_MICRO_ECC;
    memcpy(boot_security_diagnostics.digest, digest, sizeof(boot_security_diagnostics.digest));

    memcpy(public_key, boot_trust_anchor_public_x, BOOT_SECURITY_P256_SIZE);
    memcpy(&public_key[BOOT_SECURITY_P256_SIZE],
           boot_trust_anchor_public_y,
           BOOT_SECURITY_P256_SIZE);
    status = uECC_verify(public_key,
                         digest,
                         OTA_SHA256_DIGEST_SIZE,
                         signature,
                         uECC_secp256r1());
    valid = (status == 1) ? 1U : 0U;
    boot_security_diagnostics.verify_status = (uint32_t)status;
    boot_security_diagnostics.signature_valid = valid;
    memset(public_key, 0, sizeof(public_key));
    return valid;
}

void boot_security_get_diagnostics(boot_security_diagnostics_t *diagnostics)
{
    if(diagnostics != NULL)
    {
        *diagnostics = boot_security_diagnostics;
    }
}

uint8_t boot_security_verify_slot(
    const ota_boot_control_record_t *record,
    uint32_t slot,
    uint32_t *control_error)
{
    const ota_firmware_descriptor_t *descriptor;
    uint8_t image_digest[OTA_SHA256_DIGEST_SIZE];
    uint32_t minimum_version;
    ota_security_status_t status;

    if(control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SIGNATURE;
    }
    if(record == NULL ||
       (slot != (uint32_t)OTA_FIRMWARE_SLOT_A &&
        slot != (uint32_t)OTA_FIRMWARE_SLOT_B))
    {
        return 0U;
    }

    descriptor = &record->slots[slot];
    if((descriptor->image_flags & OTA_IMAGE_FLAG_SIGNED) == 0U &&
       descriptor->state == (uint32_t)OTA_SLOT_STATE_CONFIRMED &&
       record->minimum_version == 0U)
    {
        if(control_error != NULL)
        {
            *control_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
        }
        return 1U;
    }

    if(!boot_security_hash_slot(
           boot_security_slot_address(slot), descriptor->image_size, image_digest) ||
       memcmp(image_digest, descriptor->image_sha256, sizeof(image_digest)) != 0)
    {
        if(control_error != NULL)
        {
            *control_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SHA256;
        }
        memset(image_digest, 0, sizeof(image_digest));
        return 0U;
    }
    memset(image_digest, 0, sizeof(image_digest));

    minimum_version = record->minimum_version;
    if(minimum_version < OTA_BOOT_COMPILED_MINIMUM_VERSION)
    {
        minimum_version = OTA_BOOT_COMPILED_MINIMUM_VERSION;
    }
    status = ota_security_check_descriptor(
        descriptor, minimum_version, boot_security_ecdsa_verify, NULL);
    if(status == OTA_SECURITY_VERSION_ROLLBACK && control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_VERSION_ROLLBACK;
    }
    else if(status == OTA_SECURITY_BAD_IMAGE_HASH && control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SHA256;
    }
    else if(status == OTA_SECURITY_OK && control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
    }
    return (status == OTA_SECURITY_OK) ? 1U : 0U;
}
