/**
 * @file test_dwin_protocol.c
 * @brief Host tests for DWIN frame construction and fixed ACK classification.
 */

#include "dwin_protocol.h"

#include <assert.h>
#include <string.h>

/** @brief Verify the CRC-enabled 0x82 frame byte order used by CHPM. */
static void test_build_write_frame(void)
{
    static const uint8_t payload[] = {0x00U, 0x3EU};
    static const uint8_t expected[] =
        {0x5AU, 0xA5U, 0x07U, 0x82U, 0x00U, 0xA0U,
         0x00U, 0x3EU, 0xDDU, 0xECU};
    uint8_t frame[DWIN_PROTOCOL_MAX_FRAME_BYTES];
    uint16_t length = 0U;

    assert(dwin_protocol_build_write(0x00A0U,
                                     payload,
                                     sizeof(payload),
                                     frame,
                                     sizeof(frame),
                                     &length));
    assert(length == sizeof(expected));
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
}

/** @brief Verify bounded argument and capacity rejection. */
static void test_build_write_rejects_invalid_arguments(void)
{
    uint8_t frame[8];
    uint16_t length = 123U;

    assert(!dwin_protocol_build_write(0U, NULL, 1U,
                                      frame, sizeof(frame), &length));
    assert(!dwin_protocol_build_write(0U, NULL, 0U,
                                      frame, 7U, &length));
}

/** @brief Verify both fixed ACK variants and reject unrelated frames. */
static void test_ack_classification(void)
{
    static const uint8_t plain[] =
        {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    static const uint8_t crc[] =
        {0x5AU, 0xA5U, 0x05U, 0x82U, 0x4FU, 0x4BU, 0xA5U, 0xEFU};
    uint8_t invalid[sizeof(crc)];

    memcpy(invalid, crc, sizeof(invalid));
    invalid[7] ^= 1U;
    assert(dwin_protocol_classify_ack(plain, sizeof(plain)) ==
           DWIN_PROTOCOL_ACK_PLAIN);
    assert(dwin_protocol_classify_ack(crc, sizeof(crc)) ==
           DWIN_PROTOCOL_ACK_CRC);
    assert(dwin_protocol_classify_ack(invalid, sizeof(invalid)) ==
           DWIN_PROTOCOL_ACK_NONE);
}

/** @brief Run every dwin_protocol host test. */
int main(void)
{
    test_build_write_frame();
    test_build_write_rejects_invalid_arguments();
    test_ack_classification();
    return 0;
}
