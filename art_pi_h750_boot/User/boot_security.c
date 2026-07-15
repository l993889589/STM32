#include "boot_security.h"

#include <string.h>

#include "boot_spi_flash.h"
#include "bsp_qspi_w25q128.h"
#include "ota_sha256.h"
#include "uECC.h"

#define BOOT_SECURITY_READ_SIZE 512U

static const uint8_t boot_public_key[64] =
{
    0x2E, 0xCF, 0x50, 0xB0, 0xBC, 0x6E, 0x2F, 0xB3,
    0x49, 0xFB, 0x63, 0x2E, 0xF9, 0x12, 0x63, 0x7B,
    0x47, 0x50, 0x6B, 0x39, 0xA7, 0x77, 0x08, 0xEE,
    0x5C, 0x13, 0x6B, 0x31, 0x71, 0x8F, 0xF6, 0xB3,
    0x9C, 0xCD, 0xA8, 0x07, 0x25, 0x9D, 0xD7, 0x24,
    0xB7, 0xAD, 0xFA, 0x15, 0x47, 0xA3, 0x35, 0xE5,
    0xCD, 0x21, 0xB0, 0x52, 0x48, 0x03, 0x2F, 0x6D,
    0x06, 0x83, 0xE8, 0x77, 0xDE, 0x8A, 0x39, 0x26
};

static void boot_security_put_u32(uint8_t *data, uint32_t value);

uint8_t boot_security_hash_spi(uint32_t address,
                               uint32_t size,
                               uint8_t digest[32])
{
    ota_sha256_context_t sha256;
    uint8_t buffer[BOOT_SECURITY_READ_SIZE];
    uint32_t offset = 0U;

    ota_sha256_init(&sha256);
    while(offset < size)
    {
        uint32_t length = size - offset;
        if(length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        if(boot_spi_flash_read(address + offset, buffer, length) != HAL_OK)
        {
            memset(buffer, 0, sizeof(buffer));
            return 0U;
        }
        ota_sha256_update(&sha256, buffer, length);
        offset += length;
    }
    ota_sha256_finish(&sha256, digest);
    memset(buffer, 0, sizeof(buffer));
    return 1U;
}

uint8_t boot_security_hash_qspi(uint32_t address,
                                uint32_t size,
                                uint8_t digest[32])
{
    ota_sha256_context_t sha256;
    uint8_t buffer[BOOT_SECURITY_READ_SIZE];
    uint32_t offset = 0U;

    ota_sha256_init(&sha256);
    while(offset < size)
    {
        uint32_t length = size - offset;
        if(length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        if(bsp_qspi_w25q128_read(address + offset,
                                 buffer,
                                 length) != HAL_OK)
        {
            memset(buffer, 0, sizeof(buffer));
            return 0U;
        }
        ota_sha256_update(&sha256, buffer, length);
        offset += length;
    }
    ota_sha256_finish(&sha256, digest);
    memset(buffer, 0, sizeof(buffer));
    return 1U;
}

uint8_t boot_security_verify_manifest_signature(
    const gateway_ota_manifest_t *manifest)
{
    uint8_t message[64];
    uint8_t digest[32];
    int result;

    if(manifest == NULL)
    {
        return 0U;
    }
    message[0] = 'H';
    message[1] = '7';
    message[2] = 'F';
    message[3] = 'W';
    boot_security_put_u32(&message[4], GATEWAY_OTA_MANIFEST_SCHEMA);
    boot_security_put_u32(&message[8], manifest->image_version);
    boot_security_put_u32(&message[12], manifest->image_size);
    boot_security_put_u32(&message[16], manifest->image_crc32);
    boot_security_put_u32(&message[20], manifest->image_flags);
    boot_security_put_u32(&message[24], manifest->load_address);
    boot_security_put_u32(&message[28], manifest->entry_address);
    memcpy(&message[32], manifest->image_sha256, 32U);
    ota_sha256_calculate(message, sizeof(message), digest);
    result = uECC_verify(boot_public_key,
                         digest,
                         sizeof(digest),
                         manifest->signature,
                         uECC_secp256r1());
    memset(message, 0, sizeof(message));
    memset(digest, 0, sizeof(digest));
    return (result == 1) ? 1U : 0U;
}

static void boot_security_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}
