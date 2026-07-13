/**
 * @file test_ld_modbus_rtu_framer.c
 * @brief Host regression tests for strict RTU T1.5/T3.5 receive framing.
 */

#include "ld_modbus_rtu_framer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Initialize a small test framer with caller-owned storage. */
static void test_init(ld_modbus_rtu_framer_t *framer,
                      uint8_t *active,
                      uint8_t *ready,
                      uint16_t capacity,
                      uint32_t baud_rate,
                      uint8_t bits_per_char)
{
    assert(ld_modbus_rtu_framer_init(framer,
                                      active,
                                      ready,
                                      capacity,
                                      baud_rate,
                                      bits_per_char));
}

/** @brief Verify calculated and fixed Modbus timing values. */
static void test_gap_calculation(void)
{
    uint32_t t15_us = 0U;
    uint32_t t35_us = 0U;

    assert(ld_modbus_rtu_char_time_us(9600U, 10U) == 1042U);
    ld_modbus_rtu_calculate_gaps(9600U, 10U, &t15_us, &t35_us);
    assert(t15_us == 1563U);
    assert(t35_us == 3646U);

    assert(ld_modbus_rtu_char_time_us(9600U, 11U) == 1146U);
    ld_modbus_rtu_calculate_gaps(9600U, 11U, &t15_us, &t35_us);
    assert(t15_us == 1719U);
    assert(t35_us == 4011U);

    ld_modbus_rtu_calculate_gaps(115200U, 10U, &t15_us, &t35_us);
    assert(t15_us == 750U);
    assert(t35_us == 1750U);
}

/** @brief Verify a continuous stream is committed only after T3.5 silence. */
static void test_normal_frame(void)
{
    ld_modbus_rtu_framer_t framer;
    uint8_t active[16];
    uint8_t ready[16];
    uint8_t output[16];
    uint16_t output_length = 0U;
    uint32_t timestamp_us = 1000U;
    static const uint8_t expected[] = {1U, 3U, 0U, 0U};

    test_init(&framer, active, ready, sizeof(active), 9600U, 10U);
    for(uint32_t index = 0U; index < sizeof(expected); index++)
    {
        ld_modbus_rtu_framer_on_byte(&framer, expected[index], timestamp_us);
        timestamp_us += framer.char_us;
    }

    ld_modbus_rtu_framer_poll(&framer,
                              framer.last_byte_us + framer.t35_us - 1U);
    assert(!ld_modbus_rtu_framer_take(&framer,
                                       output,
                                       sizeof(output),
                                       &output_length));
    ld_modbus_rtu_framer_poll(&framer,
                              framer.last_byte_us + framer.t35_us);
    assert(ld_modbus_rtu_framer_take(&framer,
                                      output,
                                      sizeof(output),
                                      &output_length));
    assert(output_length == sizeof(expected));
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    assert(framer.diag.frames_completed == 1U);
}

/** @brief Verify T1.5 violation discards all bytes until a later T3.5 gap. */
static void test_t15_discard_until_t35(void)
{
    ld_modbus_rtu_framer_t framer;
    uint8_t active[16];
    uint8_t ready[16];
    uint8_t output[16];
    uint16_t output_length = 0U;
    uint32_t timestamp_us = 1000U;

    test_init(&framer, active, ready, sizeof(active), 9600U, 10U);
    ld_modbus_rtu_framer_on_byte(&framer, 0x01U, timestamp_us);
    timestamp_us += framer.char_us;
    ld_modbus_rtu_framer_on_byte(&framer, 0x03U, timestamp_us);

    timestamp_us += framer.char_us + framer.t15_us + 1U;
    assert((timestamp_us - framer.last_byte_us) <
           (framer.char_us + framer.t35_us));
    ld_modbus_rtu_framer_on_byte(&framer, 0xAAU, timestamp_us);
    assert(framer.diag.t15_violations == 1U);
    assert(framer.discard_until_t35);

    timestamp_us += framer.char_us;
    ld_modbus_rtu_framer_on_byte(&framer, 0xBBU, timestamp_us);
    ld_modbus_rtu_framer_poll(&framer,
                              timestamp_us + framer.t35_us);
    assert(!ld_modbus_rtu_framer_take(&framer,
                                       output,
                                       sizeof(output),
                                       &output_length));
    assert(!framer.discard_until_t35);

    timestamp_us += framer.t35_us + framer.char_us;
    ld_modbus_rtu_framer_on_byte(&framer, 0x11U, timestamp_us);
    ld_modbus_rtu_framer_on_byte(&framer,
                                  0x22U,
                                  timestamp_us + framer.char_us);
    ld_modbus_rtu_framer_poll(&framer,
                              timestamp_us + framer.char_us + framer.t35_us);
    assert(ld_modbus_rtu_framer_take(&framer,
                                      output,
                                      sizeof(output),
                                      &output_length));
    assert(output_length == 2U && output[0] == 0x11U && output[1] == 0x22U);
}

/** @brief Verify an unpolled T3.5 boundary separates old and new frames. */
static void test_new_byte_after_t35(void)
{
    ld_modbus_rtu_framer_t framer;
    uint8_t active[8];
    uint8_t ready[8];
    uint8_t output[8];
    uint16_t output_length = 0U;
    uint32_t first_timestamp = 0xFFFFFF00UL;
    uint32_t next_timestamp;

    test_init(&framer, active, ready, sizeof(active), 115200U, 10U);
    ld_modbus_rtu_framer_on_byte(&framer, 0xA5U, first_timestamp);
    next_timestamp = first_timestamp + framer.char_us + framer.t35_us;
    ld_modbus_rtu_framer_on_byte(&framer, 0x5AU, next_timestamp);

    assert(ld_modbus_rtu_framer_take(&framer,
                                      output,
                                      sizeof(output),
                                      &output_length));
    assert(output_length == 1U && output[0] == 0xA5U);
    ld_modbus_rtu_framer_poll(&framer, next_timestamp + framer.t35_us);
    assert(ld_modbus_rtu_framer_take(&framer,
                                      output,
                                      sizeof(output),
                                      &output_length));
    assert(output_length == 1U && output[0] == 0x5AU);
}

/** @brief Verify overflow discards the invalid stream until T3.5 silence. */
static void test_overflow_recovery(void)
{
    ld_modbus_rtu_framer_t framer;
    uint8_t active[2];
    uint8_t ready[2];
    uint8_t output[2];
    uint16_t output_length = 0U;

    test_init(&framer, active, ready, sizeof(active), 115200U, 10U);
    ld_modbus_rtu_framer_on_byte(&framer, 1U, 100U);
    ld_modbus_rtu_framer_on_byte(&framer, 2U, 200U);
    ld_modbus_rtu_framer_on_byte(&framer, 3U, 300U);
    assert(framer.diag.overflow == 1U);
    assert(framer.discard_until_t35);
    ld_modbus_rtu_framer_poll(&framer, 300U + framer.t35_us);
    assert(!ld_modbus_rtu_framer_take(&framer,
                                       output,
                                       sizeof(output),
                                       &output_length));
}

/** @brief Run all host-side RTU framer regression cases. */
int main(void)
{
    test_gap_calculation();
    test_normal_frame();
    test_t15_discard_until_t35();
    test_new_byte_after_t35();
    test_overflow_recovery();
    puts("ld_modbus RTU framer tests passed");
    return 0;
}
