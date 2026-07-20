/**
 * @file param.c
 * @brief CHPM parameters with validated, power-loss-safe W25Q64 persistence.
 *
 * Two 4-KiB sectors form an append-only journal.  A record is published only
 * after its body is verified and its commit word is programmed.  Runtime
 * parameters are updated only after the complete flash transaction succeeds.
 */

#include "param.h"

#include "drv_w25qxx.h"

#include <stddef.h>
#include <string.h>

#define PARAM_RECORD_MAGIC       0x4D504843UL /* "CHPM" in little endian. */
#define PARAM_SCHEMA_VERSION     2U
#define PARAM_RECORD_COMMITTED   0xA55AC33CUL
#define PARAM_ERASED_WORD        0xFFFFFFFFUL
#define PARAM_SECTOR_SIZE        4096UL
#define PARAM_RECORD_STRIDE      64UL
#define PARAM_RECORDS_PER_SECTOR (PARAM_SECTOR_SIZE / PARAM_RECORD_STRIDE)
#define PARAM_PWM_MIN            4000U
#define PARAM_PWM_MAX            10000U

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define PARAM_PACKED
#elif defined(__GNUC__)
#define PARAM_PACKED __attribute__((packed))
#else
#define PARAM_PACKED __packed
#endif

typedef struct PARAM_PACKED
{
    uint8_t address;
    uint8_t fan_mode;
    uint16_t pwm_manual;
    uint16_t pwm_auto;
    uint32_t restart_count;
} param_payload_t;

typedef struct PARAM_PACKED
{
    uint32_t magic;
    uint16_t schema_version;
    uint16_t header_size;
    uint32_t sequence;
    uint16_t payload_size;
    uint16_t reserved;
    param_payload_t payload;
    uint32_t crc32;
    uint32_t commit;
} param_record_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

PARAM_T g_tParam;
VAR_T g_tVar;

static uint32_t s_sequence;
static uint32_t s_active_sector = PARAM_SLOT_A_ADDRESS;
static uint32_t s_next_record_address = PARAM_SLOT_A_ADDRESS;
static uint32_t s_spare_sector = PARAM_SLOT_B_ADDRESS;
static bool s_spare_erased = true;
static param_load_source_t s_load_source = PARAM_LOAD_DEFAULTS;
static PARAM_T s_persisted_param;
static bool s_have_persisted_param;

/** @brief Decode a little-endian 16-bit value from legacy storage. */
static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/** @brief Decode a little-endian 32-bit value from legacy storage. */
static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/** @brief Calculate the CRC-32 used by one persistent record. */
static uint32_t param_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    while(length-- != 0U)
    {
        crc ^= *bytes++;
        for(uint8_t bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

/** @brief Validate the complete persistent parameter domain. */
static bool param_values_valid(const PARAM_T *param)
{
    return param != NULL && param->Addr485 >= 1U && param->Addr485 <= 247U &&
           param->mode <= 1U &&
           param->pwm_manual >= PARAM_PWM_MIN && param->pwm_manual <= PARAM_PWM_MAX &&
           param->pwm_auto >= PARAM_PWM_MIN && param->pwm_auto <= PARAM_PWM_MAX;
}

/** @brief Fill a parameter snapshot with safe factory defaults. */
static void param_defaults(PARAM_T *param)
{
    param->Addr485 = 1U;
    param->mode = 0U;
    param->RestartCnt = 0U;
    param->pwm_manual = PARAM_PWM_MIN;
    param->pwm_auto = PARAM_PWM_MIN;
}

/** @brief Convert the public parameter structure to the packed payload. */
static void param_to_payload(const PARAM_T *param, param_payload_t *payload)
{
    payload->address = param->Addr485;
    payload->fan_mode = param->mode;
    payload->pwm_manual = param->pwm_manual;
    payload->pwm_auto = param->pwm_auto;
    payload->restart_count = param->RestartCnt;
}

/** @brief Convert a packed payload to the public parameter structure. */
static void param_from_payload(const param_payload_t *payload, PARAM_T *param)
{
    param->Addr485 = payload->address;
    param->mode = payload->fan_mode;
    param->pwm_manual = payload->pwm_manual;
    param->pwm_auto = payload->pwm_auto;
    param->RestartCnt = (uint16_t)(payload->restart_count > UINT16_MAX ?
                                   UINT16_MAX : payload->restart_count);
}

/** @brief Validate record metadata, commit marker, CRC, and payload limits. */
static bool param_record_valid(const param_record_t *record, PARAM_T *param)
{
    PARAM_T candidate;
    uint32_t crc;

    if(record->magic != PARAM_RECORD_MAGIC ||
       record->schema_version != PARAM_SCHEMA_VERSION ||
       record->header_size != offsetof(param_record_t, payload) ||
       record->payload_size != sizeof(record->payload) ||
       record->commit != PARAM_RECORD_COMMITTED)
        return false;
    crc = param_crc32(record, offsetof(param_record_t, crc32));
    if(crc != record->crc32)
        return false;
    param_from_payload(&record->payload, &candidate);
    if(!param_values_valid(&candidate))
        return false;
    if(param != NULL)
        *param = candidate;
    return true;
}

/** @brief Return true when an entire record-sized flash area is erased. */
static bool param_record_is_erased(const param_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;

    for(size_t index = 0U; index < sizeof(*record); index++)
    {
        if(bytes[index] != 0xFFU)
            return false;
    }
    return true;
}

/** @brief Compare wrap-safe monotonically increasing record sequences. */
static bool sequence_is_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

/**
 * @brief Scan one journal sector for valid records and the first free entry.
 * @param sector_address Sector-aligned A or B base address.
 * @param best_record Receives the newest valid record found in this sector.
 * @param best_param Receives the payload of @p best_record.
 * @param best_address Receives the flash address of @p best_record.
 * @param free_address Receives the first completely erased record address.
 * @param sector_erased Receives true when every record location is erased.
 * @return true when every physical read completed, otherwise false.
 */
static bool param_scan_sector(uint32_t sector_address,
                              param_record_t *best_record,
                              PARAM_T *best_param,
                              uint32_t *best_address,
                              uint32_t *free_address,
                              bool *sector_erased)
{
    param_record_t record;
    PARAM_T candidate;
    bool have_valid = false;

    *best_address = 0U;
    *free_address = 0U;
    *sector_erased = true;
    for(uint32_t index = 0U; index < PARAM_RECORDS_PER_SECTOR; index++)
    {
        uint32_t address = sector_address + index * PARAM_RECORD_STRIDE;

        if(sf_read(address, &record, sizeof(record)) != SF_STATUS_OK)
            return false;
        if(*free_address == 0U && param_record_is_erased(&record))
            *free_address = address;
        if(!param_record_is_erased(&record))
            *sector_erased = false;
        if(param_record_valid(&record, &candidate) &&
           (!have_valid || sequence_is_newer(record.sequence,
                                             best_record->sequence)))
        {
            *best_record = record;
            *best_param = candidate;
            *best_address = address;
            have_valid = true;
        }
    }
    return true;
}

/** @brief Read and validate the legacy v0x101 parameter structure. */
static bool param_read_legacy(PARAM_T *param)
{
    uint8_t legacy[16];
    PARAM_T candidate;

    if(sf_read(PARAM_LEGACY_ADDRESS, legacy, sizeof(legacy)) != SF_STATUS_OK ||
       read_le32(&legacy[0]) != PARAM_LEGACY_VERSION)
        return false;
    candidate.Addr485 = legacy[5];
    candidate.mode = legacy[6];
    candidate.pwm_manual = read_le16(&legacy[8]);
    candidate.pwm_auto = read_le16(&legacy[10]);
    candidate.RestartCnt = (uint16_t)(read_le32(&legacy[12]) > UINT16_MAX ?
                                      UINT16_MAX : read_le32(&legacy[12]));
    if(!param_values_valid(&candidate))
        return false;
    *param = candidate;
    return true;
}

/** @brief Publish persistent values into the runtime mirror. */
static void param_refresh_runtime(void)
{
    g_tVar.mode = g_tParam.mode;
    g_tVar.pwm_manual = g_tParam.pwm_manual;
    g_tVar.pwm_auto = g_tParam.pwm_auto;
    g_tVar.RestartCnt = g_tParam.RestartCnt;
}

/**
 * @brief Commit a candidate snapshot and publish it only after verification.
 * @param candidate Fully validated parameter snapshot.
 * @return Typed result describing durability or preparation failure.
 */
static param_store_status_t param_store_candidate(const PARAM_T *candidate)
{
    param_record_t record;
    param_record_t verify;
    uint32_t target;
    uint32_t target_sector = s_active_sector;
    uint32_t commit_address;
    uint32_t commit = PARAM_RECORD_COMMITTED;
    bool rotate_sector = false;

    /*
     * ParamLoad discovers the first free record once.  Runtime commits advance
     * the cached address directly and never rescan a complete sector.
     */
    if(s_next_record_address != 0U)
    {
        target = s_next_record_address;
    }
    else
    {
        if(!s_spare_erased)
            return PARAM_STORE_STATUS_SPARE_NOT_READY;
        target_sector = s_spare_sector;
        target = target_sector;
        rotate_sector = true;
    }
    commit_address = target + offsetof(param_record_t, commit);

    memset(&record, 0xFF, sizeof(record));
    record.magic = PARAM_RECORD_MAGIC;
    record.schema_version = PARAM_SCHEMA_VERSION;
    record.header_size = (uint16_t)offsetof(param_record_t, payload);
    record.sequence = s_sequence + 1U;
    record.payload_size = (uint16_t)sizeof(record.payload);
    record.reserved = 0U;
    param_to_payload(candidate, &record.payload);
    record.crc32 = param_crc32(&record, offsetof(param_record_t, crc32));
    record.commit = PARAM_ERASED_WORD;

    if(sf_program(target, &record, sizeof(record)) != SF_STATUS_OK ||
       sf_verify(target, &record, sizeof(record)) != SF_STATUS_OK ||
       sf_program(commit_address, &commit, sizeof(commit)) != SF_STATUS_OK ||
       sf_read(target, &verify, sizeof(verify)) != SF_STATUS_OK ||
       !param_record_valid(&verify, NULL))
        return PARAM_STORE_STATUS_FLASH_ERROR;

    s_sequence = record.sequence;
    if(rotate_sector)
    {
        s_spare_sector = s_active_sector;
        s_active_sector = target_sector;
        s_spare_erased = false;
    }
    s_next_record_address =
        target + PARAM_RECORD_STRIDE < target_sector + PARAM_SECTOR_SIZE ?
        target + PARAM_RECORD_STRIDE : 0U;
    s_persisted_param = *candidate;
    s_have_persisted_param = true;
    g_tParam = *candidate;
    return PARAM_STORE_STATUS_OK;
}

/** @brief Load the newest valid journal record or migrate safe defaults. */
void ParamLoad(void)
{
    param_record_t slot_a;
    param_record_t slot_b;
    PARAM_T param_a;
    PARAM_T param_b;
    uint32_t address_a;
    uint32_t address_b;
    uint32_t free_a;
    uint32_t free_b;
    bool erased_a;
    bool erased_b;
    bool scan_a;
    bool scan_b;
    bool valid_a;
    bool valid_b;

    memset(&g_tVar, 0, sizeof(g_tVar));
    g_tVar.DevWarning = (uint16_t)(1U << 3);
    if(sf_last_status() != SF_STATUS_OK)
    {
        param_defaults(&g_tParam);
        s_have_persisted_param = false;
        s_load_source = PARAM_LOAD_FLASH_ERROR;
        param_refresh_runtime();
        return;
    }

    scan_a = param_scan_sector(PARAM_SLOT_A_ADDRESS, &slot_a, &param_a,
                               &address_a, &free_a, &erased_a);
    scan_b = param_scan_sector(PARAM_SLOT_B_ADDRESS, &slot_b, &param_b,
                               &address_b, &free_b, &erased_b);
    if(!scan_a || !scan_b)
    {
        param_defaults(&g_tParam);
        s_have_persisted_param = false;
        s_load_source = PARAM_LOAD_FLASH_ERROR;
        param_refresh_runtime();
        return;
    }
    valid_a = address_a != 0U;
    valid_b = address_b != 0U;
    if(valid_a && (!valid_b || sequence_is_newer(slot_a.sequence, slot_b.sequence)))
    {
        g_tParam = param_a;
        s_sequence = slot_a.sequence;
        s_active_sector = PARAM_SLOT_A_ADDRESS;
        s_next_record_address = free_a;
        s_spare_sector = PARAM_SLOT_B_ADDRESS;
        s_spare_erased = erased_b;
        s_load_source = PARAM_LOAD_SLOT_A;
        s_persisted_param = g_tParam;
        s_have_persisted_param = true;
    }
    else if(valid_b)
    {
        g_tParam = param_b;
        s_sequence = slot_b.sequence;
        s_active_sector = PARAM_SLOT_B_ADDRESS;
        s_next_record_address = free_b;
        s_spare_sector = PARAM_SLOT_A_ADDRESS;
        s_spare_erased = erased_a;
        s_load_source = PARAM_LOAD_SLOT_B;
        s_persisted_param = g_tParam;
        s_have_persisted_param = true;
    }
    else
    {
        if(param_read_legacy(&g_tParam))
            s_load_source = PARAM_LOAD_LEGACY;
        else
        {
            param_defaults(&g_tParam);
            s_load_source = PARAM_LOAD_DEFAULTS;
        }
        s_sequence = 0U;
        s_active_sector = PARAM_SLOT_A_ADDRESS;
        s_next_record_address = free_a;
        s_spare_sector = PARAM_SLOT_B_ADDRESS;
        s_spare_erased = erased_b;
        s_have_persisted_param = false;
        (void)param_store_candidate(&g_tParam);
    }
    param_refresh_runtime();
}

/** @brief Commit a complete candidate through the cached journal position. */
param_store_status_t ParamCommit(const PARAM_T *candidate)
{
    if(!param_values_valid(candidate))
        return PARAM_STORE_STATUS_INVALID_ARGUMENT;
    if(s_have_persisted_param &&
       memcmp(candidate, &s_persisted_param, sizeof(*candidate)) == 0)
    {
        g_tParam = *candidate;
        return PARAM_STORE_STATUS_OK;
    }
    return param_store_candidate(candidate);
}

/** @brief Publish the durable parameters into the caller-protected live state. */
void ParamPublishRuntime(void)
{
    param_refresh_runtime();
}

/** @brief Return whether the inactive journal sector still contains records. */
bool ParamSpareNeedsErase(void)
{
    return !s_spare_erased;
}

/** @brief Erase the inactive sector outside the Modbus response critical path. */
param_store_status_t ParamPrepareSpare(void)
{
    if(s_spare_erased)
        return PARAM_STORE_STATUS_OK;
    if(sf_erase_sector_checked(s_spare_sector) != SF_STATUS_OK)
        return PARAM_STORE_STATUS_FLASH_ERROR;
    s_spare_erased = true;
    return PARAM_STORE_STATUS_OK;
}

/** @brief Return the source selected by the most recent load operation. */
param_load_source_t ParamLoadSource(void)
{
    return s_load_source;
}

/** @brief Return the latest durable journal sequence number. */
uint32_t ParamSequence(void)
{
    return s_sequence;
}

/** @brief Return a copy of the current runtime parameters. */
PARAM_T get_param(void) { return g_tParam; }

/** @brief Return the current RS485 address. */
uint8_t param_rs485_addr_get(void) { return g_tParam.Addr485; }

/** @brief Return the current fan mode. */
uint8_t param_fan_mode_get(void) { return g_tParam.mode; }

/** @brief Return the current manual PWM preset. */
uint16_t param_pwm_manual_get(void) { return g_tParam.pwm_manual; }

/** @brief Return the current automatic PWM preset. */
uint16_t param_pwm_auto_get(void) { return g_tParam.pwm_auto; }

/** @brief Set or clear one bit in a 16-bit status word. */
static void set_bit(uint16_t *word, uint8_t bit_position, uint8_t value)
{
    if(word == NULL || bit_position >= 16U) return;
    if(value != 0U) *word |= (uint16_t)(1U << bit_position);
    else *word &= (uint16_t)~(uint16_t)(1U << bit_position);
}

/** @brief Set or clear one disk-initialization status bit. */
void SetDiskInitStatusBit(uint8_t bit_position, uint8_t value)
{
    set_bit(&g_tVar.DiskInitStatus, bit_position, value);
}

/** @brief Read one disk-initialization status bit. */
uint8_t GetDiskInitStatusBit(uint8_t bit_position)
{
    return bit_position < 16U ?
           (uint8_t)((g_tVar.DiskInitStatus >> bit_position) & 1U) : 0U;
}

/** @brief Set or clear one caller-provided warning word bit. */
void SetDevWarningBit(uint16_t *warning, uint8_t bit_position, uint8_t value)
{
    set_bit(warning, bit_position, value);
}

/** @brief Set or clear one global device warning bit. */
void SetWarningBit(uint8_t bit_position, uint8_t value)
{
    set_bit(&g_tVar.DevWarning, bit_position, value);
}

/** @brief Convert a disk event address into its global warning bit. */
void SetDiskErrorStatusBit(uint16_t event_addr)
{
    if(event_addr >= 0x1500U && event_addr <= 0x1505U)
        SetWarningBit((uint8_t)(event_addr - 0x1500U + 4U), 1U);
}

/** @brief Update one typed runtime telemetry field. */
uint8_t SetGlobalVar(VarType type, void *value)
{
    uint16_t input;

    if(value == NULL) return 0U;
    input = *(const uint16_t *)value;
    switch(type)
    {
        case VAR_CPU_USAGE: g_tVar.CpuUsage = input; break;
        case VAR_CPU_TEMP: g_tVar.CpuTemp = input; break;
        case VAR_FAN_RATE: g_tVar.FanRate = input; break;
        case VAR_CPU_SPEED: g_tVar.CpuSpeed = input; break;
        case VAR_GPU_USAGE: g_tVar.GpuUsage = input; break;
        case VAR_GPU_TEMP: g_tVar.GpuTemp = input; break;
        case VAR_GPU_SPEED: g_tVar.GpuSpeed = input; break;
        case VAR_RAM_USAGE: g_tVar.RamUsage = input; break;
        case VAR_RAM_AVAIL_MEMORY: g_tVar.RamAvMemory = input; break;
        case VAR_MB_VBAT_VOLTAGE: g_tVar.MbVbatV = input; break;
        case VAR_DISK_USAGE_1: case VAR_DISK_USAGE_2: case VAR_DISK_USAGE_3:
        case VAR_DISK_USAGE_4: case VAR_DISK_USAGE_5: case VAR_DISK_USAGE_6:
            g_tVar.DiskUsage[type - VAR_DISK_USAGE_1] = input; break;
        case DEV_PWM: g_tVar.pwm_manual = input; break;
        case AHT20TEMP: g_tVar.AHT20TEMP = input; break;
        case AHT20HUMI: g_tVar.AHT20HUMI = input; break;
        case DS18B20: g_tVar.DS18B20 = input; break;
        case RESTARTCNT: g_tVar.RestartCnt = input; break;
        default: return 0U;
    }
    return 1U;
}
