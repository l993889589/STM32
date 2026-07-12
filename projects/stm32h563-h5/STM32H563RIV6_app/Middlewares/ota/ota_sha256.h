/*
 * Small streaming SHA-256 implementation shared by App and Boot.
 *
 * The API uses caller-owned storage and performs no allocation, which allows
 * the same implementation to hash HTTP blocks, external slots and host tests.
 */
#ifndef OTA_SHA256_H
#define OTA_SHA256_H

#include <stdint.h>

#define OTA_SHA256_DIGEST_SIZE 32U
#define OTA_SHA256_BLOCK_SIZE  64U

typedef struct
{
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[OTA_SHA256_BLOCK_SIZE];
    uint32_t buffer_size;
} ota_sha256_context_t;

void ota_sha256_init(ota_sha256_context_t *context);
void ota_sha256_update(
    ota_sha256_context_t *context,
    const uint8_t *data,
    uint32_t size);
void ota_sha256_finish(
    ota_sha256_context_t *context,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE]);
void ota_sha256_calculate(
    const uint8_t *data,
    uint32_t size,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE]);

#endif /* OTA_SHA256_H */
