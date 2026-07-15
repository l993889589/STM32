/**
 * @file test_blackbox_store.c
 * @brief Host tests for black-box recovery, rollover, clear, and torn writes.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "blackbox_store.h"

#define TEST_SECTOR_SIZE 4096U
#define TEST_SECTOR_COUNT 4U
#define TEST_FLASH_SIZE (TEST_SECTOR_SIZE * TEST_SECTOR_COUNT)

typedef struct
{
    uint8_t bytes[TEST_FLASH_SIZE];
    bool fail_next_program;
} test_flash_t;

/** @brief Copy bytes from the simulated NOR array. */
static bool test_read(uint32_t address,
                      uint8_t *data,
                      uint32_t size,
                      void *context)
{
    test_flash_t *flash = (test_flash_t *)context;

    if((address + size) > TEST_FLASH_SIZE)
    {
        return false;
    }
    (void)memcpy(data, &flash->bytes[address], size);
    return true;
}

/** @brief Erase one aligned simulated sector. */
static bool test_erase(uint32_t address, void *context)
{
    test_flash_t *flash = (test_flash_t *)context;

    if(((address % TEST_SECTOR_SIZE) != 0U) ||
       ((address + TEST_SECTOR_SIZE) > TEST_FLASH_SIZE))
    {
        return false;
    }
    (void)memset(&flash->bytes[address], 0xFF, TEST_SECTOR_SIZE);
    return true;
}

/** @brief Program only one-to-zero transitions and optionally tear a write. */
static bool test_program(uint32_t address,
                         const uint8_t *data,
                         uint32_t size,
                         void *context)
{
    test_flash_t *flash = (test_flash_t *)context;
    uint32_t limit = size;
    uint32_t index;

    if((address + size) > TEST_FLASH_SIZE)
    {
        return false;
    }
    if(flash->fail_next_program)
    {
        flash->fail_next_program = false;
        limit = size / 2U;
    }
    for(index = 0U; index < limit; ++index)
    {
        if((uint8_t)(flash->bytes[address + index] & data[index]) != data[index])
        {
            return false;
        }
        flash->bytes[address + index] &= data[index];
    }
    return limit == size;
}

/** @brief Build one deterministic event for sequence and payload checks. */
static blackbox_store_event_t test_event(uint32_t value)
{
    blackbox_store_event_t event;

    (void)memset(&event, 0, sizeof(event));
    event.type = 2U;
    event.severity = 1U;
    event.flags = 1U;
    event.source = 7U;
    event.code = (uint16_t)value;
    event.rtc_seconds_2000 = 1000U + value;
    event.uptime_ms = value * 10U;
    event.payload_length = 4U;
    (void)memcpy(event.payload, &value, sizeof(value));
    return event;
}

/** @brief Exercise recovery, sector rollover, torn records, and logical clear. */
int main(void)
{
    static test_flash_t flash;
    blackbox_store_t store;
    blackbox_store_t recovered;
    blackbox_store_io_t io;
    blackbox_store_config_t config;
    blackbox_store_stats_t stats;
    blackbox_store_record_t tail[8];
    uint32_t value;
    uint32_t index;
    uint16_t count;

    (void)memset(&flash, 0xFF, sizeof(flash));
    flash.fail_next_program = false;
    io.read = test_read;
    io.erase_sector = test_erase;
    io.program = test_program;
    io.context = &flash;
    config.base_address = 0U;
    config.size_bytes = TEST_FLASH_SIZE;
    config.sector_size_bytes = TEST_SECTOR_SIZE;

    assert(blackbox_store_init(&store, &io, &config));
    blackbox_store_get_stats(&store, &stats);
    assert(stats.record_capacity == 252U);
    assert(stats.stored_records == 0U);

    for(index = 1U; index <= 70U; ++index)
    {
        blackbox_store_event_t event = test_event(index);
        assert(blackbox_store_append(&store, &event));
    }
    blackbox_store_get_stats(&store, &stats);
    assert(stats.stored_records == 70U);
    assert(stats.active_sector == 1U);

    assert(blackbox_store_init(&recovered, &io, &config));
    blackbox_store_get_stats(&recovered, &stats);
    assert(stats.stored_records == 70U);
    assert(stats.newest_sequence == 70U);
    assert(stats.recovered_records == 70U);
    count = blackbox_store_read_tail(&recovered, tail, 8U);
    assert(count == 8U);
    assert(tail[0].sequence == 63U);
    assert(tail[7].sequence == 70U);
    (void)memcpy(&value, tail[7].event.payload, sizeof(value));
    assert(value == 70U);

    flash.fail_next_program = true;
    {
        blackbox_store_event_t event = test_event(71U);
        assert(!blackbox_store_append(&recovered, &event));
    }
    {
        blackbox_store_event_t event = test_event(72U);
        assert(blackbox_store_append(&recovered, &event));
    }
    assert(blackbox_store_init(&store, &io, &config));
    blackbox_store_get_stats(&store, &stats);
    assert(stats.corrupt_records >= 1U);
    assert(stats.newest_sequence == 71U);

    assert(blackbox_store_clear(&store));
    blackbox_store_get_stats(&store, &stats);
    assert(stats.generation == 2U);
    assert(stats.stored_records == 0U);
    {
        blackbox_store_event_t event = test_event(100U);
        assert(blackbox_store_append(&store, &event));
    }
    assert(blackbox_store_init(&recovered, &io, &config));
    blackbox_store_get_stats(&recovered, &stats);
    assert(stats.generation == 2U);
    assert(stats.stored_records == 1U);
    count = blackbox_store_read_tail(&recovered, tail, 8U);
    assert(count == 1U);
    assert(tail[0].sequence == 1U);

    (void)puts("blackbox_store host tests passed");
    return 0;
}
