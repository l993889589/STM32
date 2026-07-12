/**
 * @file blackbox_store.h
 * @brief Power-loss-safe circular event journal over sector-based NOR storage.
 */

#ifndef BLACKBOX_STORE_H
#define BLACKBOX_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define BLACKBOX_STORE_RECORD_SIZE          64U
#define BLACKBOX_STORE_PAYLOAD_SIZE         28U
#define BLACKBOX_STORE_MAX_SECTORS          256U
#define BLACKBOX_STORE_MAX_TAIL_RECORDS     32U

typedef bool (*blackbox_store_read_fn)(uint32_t address,
                                       uint8_t *data,
                                       uint32_t size,
                                       void *context);
typedef bool (*blackbox_store_erase_fn)(uint32_t address, void *context);
typedef bool (*blackbox_store_program_fn)(uint32_t address,
                                          const uint8_t *data,
                                          uint32_t size,
                                          void *context);

typedef struct
{
    blackbox_store_read_fn read;
    blackbox_store_erase_fn erase_sector;
    blackbox_store_program_fn program;
    void *context;
} blackbox_store_io_t;

typedef struct
{
    uint32_t base_address;
    uint32_t size_bytes;
    uint32_t sector_size_bytes;
} blackbox_store_config_t;

typedef struct
{
    uint8_t type;
    uint8_t severity;
    uint8_t flags;
    uint16_t source;
    uint16_t code;
    uint32_t rtc_seconds_2000;
    uint32_t uptime_ms;
    uint16_t payload_length;
    uint8_t payload[BLACKBOX_STORE_PAYLOAD_SIZE];
} blackbox_store_event_t;

typedef struct
{
    uint32_t sequence;
    blackbox_store_event_t event;
} blackbox_store_record_t;

typedef struct
{
    uint32_t generation;
    uint32_t newest_sequence;
    uint32_t stored_records;
    uint32_t record_capacity;
    uint32_t recovered_records;
    uint32_t corrupt_records;
    uint32_t io_errors;
    uint32_t erase_operations;
    uint32_t program_operations;
    uint16_t active_sector;
    uint16_t active_slot;
    bool is_initialized;
} blackbox_store_stats_t;

typedef struct
{
    uint32_t generation;
    uint32_t sector_sequence;
    uint16_t valid_records;
    uint8_t is_valid;
} blackbox_store_sector_t;

typedef struct
{
    blackbox_store_io_t io;
    blackbox_store_config_t config;
    blackbox_store_sector_t sectors[BLACKBOX_STORE_MAX_SECTORS];
    blackbox_store_stats_t stats;
    uint32_t sector_count;
    uint32_t active_sector_sequence;
    uint32_t next_record_sequence;
    uint16_t records_per_sector;
} blackbox_store_t;

/**
 * @brief Recover or create a journal without erasing valid existing records.
 * @param store Static caller-owned journal context.
 * @param io Bounded read, sector erase, and page-safe program callbacks.
 * @param config Sector-aligned storage region configuration.
 * @return true when a writable active sector is available.
 */
bool blackbox_store_init(blackbox_store_t *store,
                         const blackbox_store_io_t *io,
                         const blackbox_store_config_t *config);

/**
 * @brief Append one event using CRC validation and a final commit word.
 * @param store Initialized journal context.
 * @param event Event metadata and bounded payload copied before returning.
 * @return true only after read-back validation succeeds.
 */
bool blackbox_store_append(blackbox_store_t *store,
                           const blackbox_store_event_t *event);

/**
 * @brief Read the newest valid records in chronological order.
 * @param store Initialized journal context.
 * @param records Caller buffer receiving up to max_records entries.
 * @param max_records Requested tail size, limited to 32.
 * @return Number of records returned.
 */
uint16_t blackbox_store_read_tail(blackbox_store_t *store,
                                  blackbox_store_record_t *records,
                                  uint16_t max_records);

/**
 * @brief Logically clear the journal by starting a new generation.
 * @param store Initialized journal context.
 * @return true after the new generation header is committed and verified.
 */
bool blackbox_store_clear(blackbox_store_t *store);

/**
 * @brief Copy current journal capacity, recovery, and error counters.
 * @param store Journal context.
 * @param stats Destination snapshot.
 */
void blackbox_store_get_stats(const blackbox_store_t *store,
                              blackbox_store_stats_t *stats);

#endif /* BLACKBOX_STORE_H */
