/*
 * STM32H563 HASH policy and PKA ECDSA-P256 verification adapter.
 *
 * Curve values and signatures are big-endian byte arrays as required by HAL
 * PKA. PKA RAM is cleared after every verification attempt.
 */
#include "boot_security.h"

#include "boot_trust_anchor.h"
#include "gd25lq128.h"
#include "main.h"
#include "ota_security_policy.h"
#include "ota_sha256.h"
#include <string.h>

#define BOOT_SECURITY_P256_SIZE 32U
#define BOOT_SECURITY_READ_SIZE 512U
#define BOOT_SECURITY_PKA_TIMEOUT_MS 1000U

static const uint8_t boot_security_p256_a[BOOT_SECURITY_P256_SIZE] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
};

static const uint8_t boot_security_p256_p[BOOT_SECURITY_P256_SIZE] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t boot_security_p256_gx[BOOT_SECURITY_P256_SIZE] =
{
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47,
    0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
    0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0,
    0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96
};

static const uint8_t boot_security_p256_gy[BOOT_SECURITY_P256_SIZE] =
{
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B,
    0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
    0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE,
    0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5
};

static const uint8_t boot_security_p256_n[BOOT_SECURITY_P256_SIZE] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84,
    0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51
};

static PKA_HandleTypeDef boot_security_pka;
static uint8_t boot_security_pka_initialized;

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

static uint8_t boot_security_pka_verify(
    void *context,
    const uint8_t digest[OTA_SHA256_DIGEST_SIZE],
    const uint8_t signature[64])
{
    PKA_ECDSAVerifInTypeDef input;
    HAL_StatusTypeDef status;
    uint8_t valid;

    (void)context;
    if(boot_security_pka_initialized == 0U)
    {
        __HAL_RCC_PKA_CLK_ENABLE();
        boot_security_pka.Instance = PKA;
        if(HAL_PKA_Init(&boot_security_pka) != HAL_OK)
        {
            return 0U;
        }
        boot_security_pka_initialized = 1U;
    }

    memset(&input, 0, sizeof(input));
    input.primeOrderSize = BOOT_SECURITY_P256_SIZE;
    input.modulusSize = BOOT_SECURITY_P256_SIZE;
    input.coefSign = 1U;
    input.coef = boot_security_p256_a;
    input.modulus = boot_security_p256_p;
    input.basePointX = boot_security_p256_gx;
    input.basePointY = boot_security_p256_gy;
    input.pPubKeyCurvePtX = boot_trust_anchor_public_x;
    input.pPubKeyCurvePtY = boot_trust_anchor_public_y;
    input.RSign = signature;
    input.SSign = &signature[BOOT_SECURITY_P256_SIZE];
    input.hash = digest;
    input.primeOrder = boot_security_p256_n;

    status = HAL_PKA_ECDSAVerif(
        &boot_security_pka, &input, BOOT_SECURITY_PKA_TIMEOUT_MS);
    valid = (status == HAL_OK &&
             HAL_PKA_ECDSAVerif_IsValidSignature(&boot_security_pka) != 0U) ? 1U : 0U;
    HAL_PKA_RAMReset(&boot_security_pka);
    return valid;
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
        descriptor, minimum_version, boot_security_pka_verify, NULL);
    if(status == OTA_SECURITY_VERSION_ROLLBACK && control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_VERSION_ROLLBACK;
    }
    else if(status == OTA_SECURITY_BAD_IMAGE_HASH && control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SHA256;
    }
    return (status == OTA_SECURITY_OK) ? 1U : 0U;
}
