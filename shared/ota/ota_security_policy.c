/*
 * Canonical OTA signature policy implementation.
 *
 * The signature covers "H5FW", schema, all boot-relevant scalar metadata and
 * the image SHA-256. Slot state is deliberately excluded because it changes
 * as the same immutable package moves through download, trial and confirmed.
 */
#include "ota_security_policy.h"

#include <string.h>

#define OTA_SECURITY_MESSAGE_SIZE 64U

static void ota_security_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint8_t ota_security_all_zero(const uint8_t *data, uint32_t size)
{
    uint8_t value = 0U;
    while(size-- != 0U)
    {
        value |= *data++;
    }
    return (value == 0U) ? 1U : 0U;
}

void ota_security_manifest_digest(
    const ota_firmware_descriptor_t *descriptor,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE])
{
    uint8_t message[OTA_SECURITY_MESSAGE_SIZE];

    if(descriptor == NULL || digest == NULL)
    {
        return;
    }

    message[0] = 'H';
    message[1] = '5';
    message[2] = 'F';
    message[3] = 'W';
    ota_security_put_u32(&message[4], OTA_SECURITY_MANIFEST_SCHEMA);
    ota_security_put_u32(&message[8], descriptor->image_version);
    ota_security_put_u32(&message[12], descriptor->image_size);
    ota_security_put_u32(&message[16], descriptor->image_crc32);
    ota_security_put_u32(&message[20], descriptor->image_flags);
    ota_security_put_u32(&message[24], descriptor->load_address);
    ota_security_put_u32(&message[28], descriptor->entry_address);
    memcpy(&message[32], descriptor->image_sha256, OTA_SHA256_DIGEST_SIZE);
    ota_sha256_calculate(message, sizeof(message), digest);
    memset(message, 0, sizeof(message));
}

ota_security_status_t ota_security_check_descriptor(
    const ota_firmware_descriptor_t *descriptor,
    uint32_t minimum_version,
    ota_security_verify_fn verify,
    void *verify_context)
{
    uint8_t digest[OTA_SHA256_DIGEST_SIZE];

    if(descriptor == NULL || verify == NULL)
    {
        return OTA_SECURITY_INVALID_ARGUMENT;
    }
    if((descriptor->image_flags & OTA_IMAGE_FLAG_SIGNED) == 0U)
    {
        return OTA_SECURITY_UNSIGNED;
    }
    if(descriptor->image_version < minimum_version)
    {
        return OTA_SECURITY_VERSION_ROLLBACK;
    }
    if(ota_security_all_zero(descriptor->image_sha256,
                             sizeof(descriptor->image_sha256)) != 0U)
    {
        return OTA_SECURITY_BAD_IMAGE_HASH;
    }
    if(ota_security_all_zero(descriptor->signature,
                             sizeof(descriptor->signature)) != 0U)
    {
        return OTA_SECURITY_BAD_SIGNATURE;
    }

    ota_security_manifest_digest(descriptor, digest);
    if(verify(verify_context, digest, descriptor->signature) == 0U)
    {
        memset(digest, 0, sizeof(digest));
        return OTA_SECURITY_BAD_SIGNATURE;
    }
    memset(digest, 0, sizeof(digest));
    return OTA_SECURITY_OK;
}
