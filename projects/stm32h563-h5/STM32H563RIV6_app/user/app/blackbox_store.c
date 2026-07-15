/**
 * @file blackbox_store.c
 * @brief Sector wear-leveling and two-phase record commit for the black box.
 */

#include "blackbox_store.h"

#include <stddef.h>
#include <string.h>

#define BLACKBOX_SECTOR_MAGIC              0x42585348UL
#define BLACKBOX_RECORD_MAGIC              0x42585243UL
#define BLACKBOX_COMMIT_WORD               0x434F4D54UL
#define BLACKBOX_SCHEMA_VERSION            1U
#define BLACKBOX_HEADER_SIZE               64U
#define BLACKBOX_ERASED_BYTE               0xFFU

typedef struct
{
    uint32_t magic;
    uint16_t schema_version;
    uint16_t header_size;
    uint32_t generation;
    uint32_t sector_sequence;
    uint32_t first_record_sequence;
    uint8_t reserved[36];
    uint32_t crc32;
    uint32_t commit_word;
} blackbox_sector_header_t;

typedef struct
{
    uint32_t magic;
    uint8_t schema_version;
    uint8_t type;
    uint8_t severity;
    uint8_t flags;
    uint32_t sequence;
    uint32_t rtc_seconds_2000;
    uint32_t uptime_ms;
    uint16_t source;
    uint16_t code;
    uint16_t payload_length;
    uint16_t reserved;
    uint8_t payload[BLACKBOX_STORE_PAYLOAD_SIZE];
    uint32_t crc32;
    uint32_t commit_word;
} blackbox_physical_record_t;

typedef char blackbox_header_size_must_be_64[
    (sizeof(blackbox_sector_header_t) == BLACKBOX_HEADER_SIZE) ? 1 : -1];
typedef char blackbox_record_size_must_be_64[
    (sizeof(blackbox_physical_record_t) == BLACKBOX_STORE_RECORD_SIZE) ? 1 : -1];

/** @brief Calculate the Ethernet/ZIP reflected CRC32 used by journal objects. */
static uint32_t blackbox_crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t index;

    for(index = 0U; index < size; ++index)
    {
        uint32_t bit;

        crc ^= data[index];
        for(bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^
                  ((0U - (crc & 1U)) & 0xEDB88320UL);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/** @brief Compare wrapping 32-bit monotonic values. */
static bool blackbox_sequence_is_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

/** @brief Return true when a buffer contains only erased NOR bytes. */
static bool blackbox_is_erased(const uint8_t *data, uint32_t size)
{
    uint32_t index;

    for(index = 0U; index < size; ++index)
    {
        if(data[index] != BLACKBOX_ERASED_BYTE)
        {
            return false;
        }
    }
    return true;
}

/** @brief Convert a sector index to its physical flash address. */
static uint32_t blackbox_sector_address(const blackbox_store_t *store,
                                        uint32_t sector_index)
{
    return store->config.base_address +
           (sector_index * store->config.sector_size_bytes);
}

/** @brief Convert a sector/slot pair to a physical record address. */
static uint32_t blackbox_record_address(const blackbox_store_t *store,
                                        uint32_t sector_index,
                                        uint32_t slot_index)
{
    return blackbox_sector_address(store, sector_index) +
           BLACKBOX_HEADER_SIZE +
           (slot_index * BLACKBOX_STORE_RECORD_SIZE);
}

/** @brief Read a flash fragment and account for transport failures. */
static bool blackbox_read(blackbox_store_t *store,
                          uint32_t address,
                          void *data,
                          uint32_t size)
{
    if(!store->io.read(address, (uint8_t *)data, size, store->io.context))
    {
        ++store->stats.io_errors;
        return false;
    }
    return true;
}

/** @brief Program a flash fragment and account for program operations. */
static bool blackbox_program(blackbox_store_t *store,
                             uint32_t address,
                             const void *data,
                             uint32_t size)
{
    ++store->stats.program_operations;
    if(!store->io.program(address,
                          (const uint8_t *)data,
                          size,
                          store->io.context))
    {
        ++store->stats.io_errors;
        return false;
    }
    return true;
}

/** @brief Validate a committed sector header and its CRC. */
static bool blackbox_header_is_valid(const blackbox_sector_header_t *header)
{
    return (header->magic == BLACKBOX_SECTOR_MAGIC) &&
           (header->schema_version == BLACKBOX_SCHEMA_VERSION) &&
           (header->header_size == BLACKBOX_HEADER_SIZE) &&
           (header->commit_word == BLACKBOX_COMMIT_WORD) &&
           (header->crc32 == blackbox_crc32((const uint8_t *)header,
                                            offsetof(blackbox_sector_header_t,
                                                     crc32)));
}

/** @brief Validate a committed record, bounded payload length, and CRC. */
static bool blackbox_record_is_valid(const blackbox_physical_record_t *record)
{
    return (record->magic == BLACKBOX_RECORD_MAGIC) &&
           (record->schema_version == BLACKBOX_SCHEMA_VERSION) &&
           (record->payload_length <= BLACKBOX_STORE_PAYLOAD_SIZE) &&
           (record->commit_word == BLACKBOX_COMMIT_WORD) &&
           (record->crc32 == blackbox_crc32((const uint8_t *)record,
                                            offsetof(blackbox_physical_record_t,
                                                     crc32)));
}

/** @brief Commit a fresh header after erasing its destination sector. */
static bool blackbox_create_sector(blackbox_store_t *store,
                                   uint32_t sector_index,
                                   uint32_t generation,
                                   uint32_t sector_sequence,
                                   uint32_t first_record_sequence)
{
    blackbox_sector_header_t header;
    blackbox_sector_header_t verify;
    uint32_t address = blackbox_sector_address(store, sector_index);
    uint32_t commit_word = BLACKBOX_COMMIT_WORD;

    ++store->stats.erase_operations;
    if(!store->io.erase_sector(address, store->io.context))
    {
        ++store->stats.io_errors;
        return false;
    }

    (void)memset(&header, BLACKBOX_ERASED_BYTE, sizeof(header));
    header.magic = BLACKBOX_SECTOR_MAGIC;
    header.schema_version = BLACKBOX_SCHEMA_VERSION;
    header.header_size = BLACKBOX_HEADER_SIZE;
    header.generation = generation;
    header.sector_sequence = sector_sequence;
    header.first_record_sequence = first_record_sequence;
    header.crc32 = blackbox_crc32((const uint8_t *)&header,
                                  offsetof(blackbox_sector_header_t, crc32));

    if(!blackbox_program(store,
                         address,
                         &header,
                         offsetof(blackbox_sector_header_t, commit_word)) ||
       !blackbox_program(store,
                         address + offsetof(blackbox_sector_header_t,
                                            commit_word),
                         &commit_word,
                         sizeof(commit_word)) ||
       !blackbox_read(store, address, &verify, sizeof(verify)) ||
       !blackbox_header_is_valid(&verify))
    {
        return false;
    }

    store->sectors[sector_index].is_valid = 1U;
    store->sectors[sector_index].generation = generation;
    store->sectors[sector_index].sector_sequence = sector_sequence;
    store->sectors[sector_index].valid_records = 0U;
    store->stats.active_sector = (uint16_t)sector_index;
    store->stats.active_slot = 0U;
    store->stats.generation = generation;
    store->active_sector_sequence = sector_sequence;
    return true;
}

/** @brief Advance to the next physical sector and preserve older sectors. */
static bool blackbox_rotate_sector(blackbox_store_t *store,
                                   uint32_t generation,
                                   uint32_t sector_sequence)
{
    uint32_t next_sector = ((uint32_t)store->stats.active_sector + 1U) %
                           store->sector_count;

    if(store->sectors[next_sector].is_valid != 0U &&
       store->sectors[next_sector].generation == store->stats.generation)
    {
        store->stats.stored_records -=
            store->sectors[next_sector].valid_records;
    }
    return blackbox_create_sector(store,
                                  next_sector,
                                  generation,
                                  sector_sequence,
                                  store->next_record_sequence);
}

/** @brief Scan one sector's record slots and update recovery state. */
static bool blackbox_scan_sector(blackbox_store_t *store,
                                 uint32_t sector_index,
                                 bool is_active)
{
    blackbox_physical_record_t record;
    uint32_t slot;
    uint16_t next_slot = 0U;

    store->sectors[sector_index].valid_records = 0U;
    for(slot = 0U; slot < store->records_per_sector; ++slot)
    {
        if(!blackbox_read(store,
                          blackbox_record_address(store, sector_index, slot),
                          &record,
                          sizeof(record)))
        {
            return false;
        }
        if(blackbox_is_erased((const uint8_t *)&record, sizeof(record)))
        {
            continue;
        }

        next_slot = (uint16_t)(slot + 1U);
        if(!blackbox_record_is_valid(&record))
        {
            ++store->stats.corrupt_records;
            continue;
        }

        ++store->sectors[sector_index].valid_records;
        ++store->stats.stored_records;
        ++store->stats.recovered_records;
        if((store->stats.newest_sequence == 0U) ||
           blackbox_sequence_is_newer(record.sequence,
                                      store->stats.newest_sequence))
        {
            store->stats.newest_sequence = record.sequence;
        }
    }
    if(is_active)
    {
        store->stats.active_slot = next_slot;
    }
    return true;
}

/** @brief Recover sector generations, active position, and the next sequence. */
static bool blackbox_recover(blackbox_store_t *store)
{
    blackbox_sector_header_t header;
    uint32_t sector;
    uint32_t active_sector = 0U;
    uint32_t active_generation = 0U;
    uint32_t active_sequence = 0U;
    bool found = false;

    for(sector = 0U; sector < store->sector_count; ++sector)
    {
        if(!blackbox_read(store,
                          blackbox_sector_address(store, sector),
                          &header,
                          sizeof(header)))
        {
            return false;
        }
        if(!blackbox_header_is_valid(&header))
        {
            continue;
        }

        store->sectors[sector].is_valid = 1U;
        store->sectors[sector].generation = header.generation;
        store->sectors[sector].sector_sequence = header.sector_sequence;
        if(!found ||
           blackbox_sequence_is_newer(header.generation,
                                      active_generation) ||
           ((header.generation == active_generation) &&
            blackbox_sequence_is_newer(header.sector_sequence,
                                       active_sequence)))
        {
            found = true;
            active_sector = sector;
            active_generation = header.generation;
            active_sequence = header.sector_sequence;
        }
    }

    if(!found)
    {
        store->next_record_sequence = 1U;
        return blackbox_create_sector(store, 0U, 1U, 1U, 1U);
    }

    store->stats.active_sector = (uint16_t)active_sector;
    store->stats.generation = active_generation;
    store->active_sector_sequence = active_sequence;
    for(sector = 0U; sector < store->sector_count; ++sector)
    {
        if((store->sectors[sector].is_valid != 0U) &&
           (store->sectors[sector].generation == active_generation) &&
           !blackbox_scan_sector(store, sector, sector == active_sector))
        {
            return false;
        }
    }
    store->next_record_sequence = store->stats.newest_sequence + 1U;
    if(store->next_record_sequence == 0U)
    {
        store->next_record_sequence = 1U;
    }
    return true;
}

/** @brief Recover or create a journal without erasing valid existing records. */
bool blackbox_store_init(blackbox_store_t *store,
                         const blackbox_store_io_t *io,
                         const blackbox_store_config_t *config)
{
    if((store == NULL) || (io == NULL) || (config == NULL) ||
       (io->read == NULL) || (io->erase_sector == NULL) ||
       (io->program == NULL) || (config->sector_size_bytes < 128U) ||
       ((config->base_address % config->sector_size_bytes) != 0U) ||
       ((config->size_bytes % config->sector_size_bytes) != 0U))
    {
        return false;
    }

    (void)memset(store, 0, sizeof(*store));
    store->io = *io;
    store->config = *config;
    store->sector_count = config->size_bytes / config->sector_size_bytes;
    store->records_per_sector = (uint16_t)(
        (config->sector_size_bytes - BLACKBOX_HEADER_SIZE) /
        BLACKBOX_STORE_RECORD_SIZE);
    if((store->sector_count == 0U) ||
       (store->sector_count > BLACKBOX_STORE_MAX_SECTORS) ||
       (store->records_per_sector == 0U))
    {
        return false;
    }

    store->stats.record_capacity = store->sector_count *
                                   store->records_per_sector;
    if(!blackbox_recover(store))
    {
        return false;
    }
    store->stats.is_initialized = true;
    return true;
}

/** @brief Append one event using CRC validation and a final commit word. */
bool blackbox_store_append(blackbox_store_t *store,
                           const blackbox_store_event_t *event)
{
    blackbox_physical_record_t record;
    blackbox_physical_record_t verify;
    uint32_t address;
    uint32_t commit_word = BLACKBOX_COMMIT_WORD;

    if((store == NULL) || (event == NULL) ||
       !store->stats.is_initialized ||
       (event->payload_length > BLACKBOX_STORE_PAYLOAD_SIZE))
    {
        return false;
    }
    if(store->stats.active_slot >= store->records_per_sector &&
       !blackbox_rotate_sector(store,
                               store->stats.generation,
                               store->active_sector_sequence + 1U))
    {
        return false;
    }

    (void)memset(&record, BLACKBOX_ERASED_BYTE, sizeof(record));
    record.magic = BLACKBOX_RECORD_MAGIC;
    record.schema_version = BLACKBOX_SCHEMA_VERSION;
    record.type = event->type;
    record.severity = event->severity;
    record.flags = event->flags;
    record.sequence = store->next_record_sequence;
    record.rtc_seconds_2000 = event->rtc_seconds_2000;
    record.uptime_ms = event->uptime_ms;
    record.source = event->source;
    record.code = event->code;
    record.payload_length = event->payload_length;
    if(event->payload_length > 0U)
    {
        (void)memcpy(record.payload,
                     event->payload,
                     event->payload_length);
    }
    record.crc32 = blackbox_crc32((const uint8_t *)&record,
                                  offsetof(blackbox_physical_record_t, crc32));
    address = blackbox_record_address(store,
                                      store->stats.active_sector,
                                      store->stats.active_slot);

    if(!blackbox_program(store,
                         address,
                         &record,
                         offsetof(blackbox_physical_record_t, commit_word)) ||
       !blackbox_program(store,
                         address + offsetof(blackbox_physical_record_t,
                                            commit_word),
                         &commit_word,
                         sizeof(commit_word)) ||
       !blackbox_read(store, address, &verify, sizeof(verify)) ||
       !blackbox_record_is_valid(&verify))
    {
        ++store->stats.corrupt_records;
        ++store->stats.active_slot;
        return false;
    }

    ++store->stats.active_slot;
    ++store->sectors[store->stats.active_sector].valid_records;
    ++store->stats.stored_records;
    store->stats.newest_sequence = store->next_record_sequence;
    ++store->next_record_sequence;
    if(store->next_record_sequence == 0U)
    {
        store->next_record_sequence = 1U;
    }
    return true;
}

/** @brief Insert one record into a bounded ascending tail set. */
static void blackbox_tail_insert(blackbox_store_record_t *records,
                                 uint16_t *count,
                                 uint16_t capacity,
                                 const blackbox_store_record_t *candidate)
{
    uint16_t index;

    if(*count < capacity)
    {
        records[*count] = *candidate;
        ++(*count);
    }
    else if(blackbox_sequence_is_newer(candidate->sequence,
                                       records[0].sequence))
    {
        for(index = 1U; index < capacity; ++index)
        {
            records[index - 1U] = records[index];
        }
        records[capacity - 1U] = *candidate;
    }
    else
    {
        return;
    }

    index = (uint16_t)(*count - 1U);
    while((index > 0U) &&
          blackbox_sequence_is_newer(records[index - 1U].sequence,
                                     records[index].sequence))
    {
        blackbox_store_record_t swap = records[index - 1U];

        records[index - 1U] = records[index];
        records[index] = swap;
        --index;
    }
}

/** @brief Convert one validated physical record to the public representation. */
static void blackbox_unpack_record(const blackbox_physical_record_t *physical,
                                   blackbox_store_record_t *record)
{
    (void)memset(record, 0, sizeof(*record));
    record->sequence = physical->sequence;
    record->event.type = physical->type;
    record->event.severity = physical->severity;
    record->event.flags = physical->flags;
    record->event.source = physical->source;
    record->event.code = physical->code;
    record->event.rtc_seconds_2000 = physical->rtc_seconds_2000;
    record->event.uptime_ms = physical->uptime_ms;
    record->event.payload_length = physical->payload_length;
    if(physical->payload_length > 0U)
    {
        (void)memcpy(record->event.payload,
                     physical->payload,
                     physical->payload_length);
    }
}

/** @brief Read the newest valid records in chronological order. */
uint16_t blackbox_store_read_tail(blackbox_store_t *store,
                                  blackbox_store_record_t *records,
                                  uint16_t max_records)
{
    blackbox_physical_record_t physical;
    blackbox_store_record_t candidate;
    uint32_t sector;
    uint32_t slot;
    uint16_t count = 0U;

    if((store == NULL) || (records == NULL) ||
       !store->stats.is_initialized || (max_records == 0U))
    {
        return 0U;
    }
    if(max_records > BLACKBOX_STORE_MAX_TAIL_RECORDS)
    {
        max_records = BLACKBOX_STORE_MAX_TAIL_RECORDS;
    }

    for(sector = 0U; sector < store->sector_count; ++sector)
    {
        if((store->sectors[sector].is_valid == 0U) ||
           (store->sectors[sector].generation != store->stats.generation))
        {
            continue;
        }
        for(slot = 0U; slot < store->records_per_sector; ++slot)
        {
            if(!blackbox_read(store,
                              blackbox_record_address(store, sector, slot),
                              &physical,
                              sizeof(physical)))
            {
                return count;
            }
            if(!blackbox_record_is_valid(&physical))
            {
                continue;
            }
            blackbox_unpack_record(&physical, &candidate);
            blackbox_tail_insert(records,
                                 &count,
                                 max_records,
                                 &candidate);
        }
    }
    return count;
}

/** @brief Logically clear the journal by starting a new generation. */
bool blackbox_store_clear(blackbox_store_t *store)
{
    uint32_t generation;
    uint32_t sector;

    if((store == NULL) || !store->stats.is_initialized)
    {
        return false;
    }
    generation = store->stats.generation + 1U;
    if(generation == 0U)
    {
        generation = 1U;
    }
    store->stats.stored_records = 0U;
    store->stats.recovered_records = 0U;
    store->stats.newest_sequence = 0U;
    store->next_record_sequence = 1U;
    for(sector = 0U; sector < store->sector_count; ++sector)
    {
        store->sectors[sector].is_valid = 0U;
        store->sectors[sector].valid_records = 0U;
    }
    return blackbox_rotate_sector(store, generation, 1U);
}

/** @brief Copy current journal capacity, recovery, and error counters. */
void blackbox_store_get_stats(const blackbox_store_t *store,
                              blackbox_store_stats_t *stats)
{
    if((store != NULL) && (stats != NULL))
    {
        *stats = store->stats;
    }
}
