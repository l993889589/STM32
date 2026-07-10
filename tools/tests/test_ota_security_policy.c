/* Host tests for canonical manifest signing and anti-rollback policy. */
#include "ota_security_policy.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static uint8_t g_expected_digest[OTA_SHA256_DIGEST_SIZE];

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        g_failures++; \
    } } while(0)

static uint8_t test_verify(
    void *context,
    const uint8_t digest[OTA_SHA256_DIGEST_SIZE],
    const uint8_t signature[64])
{
    (void)context;
    return (memcmp(digest, g_expected_digest, sizeof(g_expected_digest)) == 0 &&
            signature[0] == 0xA5U) ? 1U : 0U;
}

static ota_firmware_descriptor_t test_descriptor(void)
{
    ota_firmware_descriptor_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.state = OTA_SLOT_STATE_VERIFIED;
    descriptor.image_version = 2026071017U;
    descriptor.image_size = 858240U;
    descriptor.image_crc32 = 0x12345678U;
    descriptor.image_flags = OTA_IMAGE_FLAG_SIGNED;
    descriptor.load_address = OTA_APP_BASE;
    descriptor.entry_address = OTA_APP_BASE + 0x43DU;
    ota_sha256_calculate((const uint8_t *)"firmware", 8U,
                         descriptor.image_sha256);
    descriptor.signature[0] = 0xA5U;
    return descriptor;
}

int main(void)
{
    ota_firmware_descriptor_t descriptor = test_descriptor();
    ota_firmware_descriptor_t tampered;

    ota_security_manifest_digest(&descriptor, g_expected_digest);
    TEST_CHECK(ota_security_check_descriptor(
                   &descriptor, 2026071016U, test_verify, NULL) == OTA_SECURITY_OK);

    tampered = descriptor;
    tampered.image_size++;
    TEST_CHECK(ota_security_check_descriptor(
                   &tampered, 2026071016U, test_verify, NULL) ==
               OTA_SECURITY_BAD_SIGNATURE);

    tampered = descriptor;
    tampered.image_flags = 0U;
    TEST_CHECK(ota_security_check_descriptor(
                   &tampered, 0U, test_verify, NULL) == OTA_SECURITY_UNSIGNED);

    tampered = descriptor;
    memset(tampered.image_sha256, 0, sizeof(tampered.image_sha256));
    TEST_CHECK(ota_security_check_descriptor(
                   &tampered, 0U, test_verify, NULL) ==
               OTA_SECURITY_BAD_IMAGE_HASH);

    TEST_CHECK(ota_security_check_descriptor(
                   &descriptor, descriptor.image_version + 1U,
                   test_verify, NULL) == OTA_SECURITY_VERSION_ROLLBACK);

    if(g_failures != 0)
    {
        printf("ota_security_policy: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("ota_security_policy: all tests passed\n");
    return 0;
}
