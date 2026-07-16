#include "param.h"

#include "drv_w25qxx.h"

#include <stddef.h>
#include <string.h>

#define PARAM_RECORD_MAGIC       0x4D504843UL /* "CHPM" in little endian. */
#define PARAM_SCHEMA_VERSION     2U
#define PARAM_RECORD_COMMITTED   0xA55AC33CUL
#define PARAM_ERASED_WORD        0xFFFFFFFFUL
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
static uint32_t s_active_slot = PARAM_SLOT_A_ADDRESS;
static param_load_source_t s_load_source = PARAM_LOAD_DEFAULTS;
static PARAM_T s_persisted_param;
static bool s_have_persisted_param;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

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

static bool param_values_valid(const PARAM_T *param)
{
    return param != NULL && param->Addr485 >= 1U && param->Addr485 <= 247U &&
           param->mode <= 1U &&
           param->pwm_manual >= PARAM_PWM_MIN && param->pwm_manual <= PARAM_PWM_MAX &&
           param->pwm_auto >= PARAM_PWM_MIN && param->pwm_auto <= PARAM_PWM_MAX;
}

static void param_defaults(PARAM_T *param)
{
    param->Addr485 = 1U;
    param->mode = 0U;
    param->RestartCnt = 0U;
    param->pwm_manual = PARAM_PWM_MIN;
    param->pwm_auto = PARAM_PWM_MIN;
}

static void param_to_payload(const PARAM_T *param, param_payload_t *payload)
{
    payload->address = param->Addr485;
    payload->fan_mode = param->mode;
    payload->pwm_manual = param->pwm_manual;
    payload->pwm_auto = param->pwm_auto;
    payload->restart_count = param->RestartCnt;
}

static void param_from_payload(const param_payload_t *payload, PARAM_T *param)
{
    param->Addr485 = payload->address;
    param->mode = payload->fan_mode;
    param->pwm_manual = payload->pwm_manual;
    param->pwm_auto = payload->pwm_auto;
    param->RestartCnt = (uint16_t)(payload->restart_count > UINT16_MAX ?
                                   UINT16_MAX : payload->restart_count);
}

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

static bool param_read_slot(uint32_t address,
                            param_record_t *record,
                            PARAM_T *param)
{
    return sf_read(address, record, sizeof(*record)) == SF_STATUS_OK &&
           param_record_valid(record, param);
}

static bool sequence_is_newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

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

static void param_refresh_runtime(void)
{
    g_tVar.mode = g_tParam.mode;
    g_tVar.pwm_manual = g_tParam.pwm_manual;
    g_tVar.pwm_auto = g_tParam.pwm_auto;
    g_tVar.RestartCnt = g_tParam.RestartCnt;
}

static bool param_store_current(void)
{
    param_record_t record;
    param_record_t verify;
    uint32_t target = s_active_slot == PARAM_SLOT_A_ADDRESS ?
                      PARAM_SLOT_B_ADDRESS : PARAM_SLOT_A_ADDRESS;
    uint32_t commit_address = target + offsetof(param_record_t, commit);
    uint32_t commit = PARAM_RECORD_COMMITTED;

    memset(&record, 0xFF, sizeof(record));
    record.magic = PARAM_RECORD_MAGIC;
    record.schema_version = PARAM_SCHEMA_VERSION;
    record.header_size = (uint16_t)offsetof(param_record_t, payload);
    record.sequence = s_sequence + 1U;
    record.payload_size = (uint16_t)sizeof(record.payload);
    record.reserved = 0U;
    param_to_payload(&g_tParam, &record.payload);
    record.crc32 = param_crc32(&record, offsetof(param_record_t, crc32));
    record.commit = PARAM_ERASED_WORD;

    if(sf_erase_sector_checked(target) != SF_STATUS_OK ||
       sf_program(target, &record, sizeof(record)) != SF_STATUS_OK ||
       sf_verify(target, &record, sizeof(record)) != SF_STATUS_OK ||
       sf_program(commit_address, &commit, sizeof(commit)) != SF_STATUS_OK ||
       sf_read(target, &verify, sizeof(verify)) != SF_STATUS_OK ||
       !param_record_valid(&verify, NULL))
        return false;

    s_sequence = record.sequence;
    s_active_slot = target;
    s_persisted_param = g_tParam;
    s_have_persisted_param = true;
    return true;
}

void ParamLoad(void)
{
    param_record_t slot_a;
    param_record_t slot_b;
    PARAM_T param_a;
    PARAM_T param_b;
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

    valid_a = param_read_slot(PARAM_SLOT_A_ADDRESS, &slot_a, &param_a);
    valid_b = param_read_slot(PARAM_SLOT_B_ADDRESS, &slot_b, &param_b);
    if(valid_a && (!valid_b || sequence_is_newer(slot_a.sequence, slot_b.sequence)))
    {
        g_tParam = param_a;
        s_sequence = slot_a.sequence;
        s_active_slot = PARAM_SLOT_A_ADDRESS;
        s_load_source = PARAM_LOAD_SLOT_A;
        s_persisted_param = g_tParam;
        s_have_persisted_param = true;
    }
    else if(valid_b)
    {
        g_tParam = param_b;
        s_sequence = slot_b.sequence;
        s_active_slot = PARAM_SLOT_B_ADDRESS;
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
        s_active_slot = PARAM_SLOT_B_ADDRESS;
        s_have_persisted_param = false;
        (void)param_store_current();
    }
    param_refresh_runtime();
}

bool Param_Store(uint8_t addr, uint8_t mode, uint16_t restart_count,
                 uint16_t pwm_manual, uint16_t pwm_auto)
{
    PARAM_T candidate;
    PARAM_T previous;

    candidate.Addr485 = addr;
    candidate.mode = mode;
    candidate.RestartCnt = restart_count;
    candidate.pwm_manual = pwm_manual;
    candidate.pwm_auto = pwm_auto;
    if(!param_values_valid(&candidate))
        return false;
    if(s_have_persisted_param &&
       memcmp(&candidate, &s_persisted_param, sizeof(candidate)) == 0)
    {
        g_tParam = candidate;
        param_refresh_runtime();
        return true;
    }
    previous = s_have_persisted_param ? s_persisted_param : g_tParam;
    g_tParam = candidate;
    param_refresh_runtime();
    if(param_store_current())
        return true;
    g_tParam = previous;
    param_refresh_runtime();
    return false;
}

param_load_source_t ParamLoadSource(void)
{
    return s_load_source;
}

uint32_t ParamSequence(void)
{
    return s_sequence;
}

PARAM_T get_param(void) { return g_tParam; }
PARAM_T ParamGet(void) { return g_tParam; }
void ParamSet(PARAM_T param) { if(param_values_valid(&param)) g_tParam = param; }

void ParamSetPlus(uint8_t addr, uint8_t mode, uint16_t restart_count)
{
    if(addr >= 1U && addr <= 247U) g_tParam.Addr485 = addr;
    g_tParam.mode = mode ? 1U : 0U;
    g_tParam.RestartCnt = restart_count;
}

void ParamStorePlus(void) { (void)param_store_current(); }
void ParamStoreEx(void) { (void)param_store_current(); }
void load_param(void) { ParamLoad(); }
void load_base_param(void) { param_defaults(&g_tParam); param_refresh_runtime(); }
void restroe_factory_param_setting(void) { load_base_param(); }

void param_rs485_addr_set(uint8_t addr)
{
    if(addr >= 1U && addr <= 247U) g_tParam.Addr485 = addr;
}

uint8_t param_rs485_addr_get(void) { return g_tParam.Addr485; }
void param_fan_mode_set(uint8_t mode) { g_tParam.mode = mode ? 1U : 0U; }
uint8_t param_fan_mode_get(void) { return g_tParam.mode; }

void param_pwm_set(uint16_t pwm) { param_pwm_manual_set(pwm); }
uint16_t param_pwm_get(void) { return param_pwm_manual_get(); }

void param_pwm_manual_set(uint16_t pwm)
{
    if(pwm >= PARAM_PWM_MIN && pwm <= PARAM_PWM_MAX) g_tParam.pwm_manual = pwm;
}

uint16_t param_pwm_manual_get(void) { return g_tParam.pwm_manual; }

void param_pwm_auto_set(uint16_t pwm)
{
    if(pwm >= PARAM_PWM_MIN && pwm <= PARAM_PWM_MAX) g_tParam.pwm_auto = pwm;
}

uint16_t param_pwm_auto_get(void) { return g_tParam.pwm_auto; }

static void set_bit(uint16_t *word, uint8_t bit_position, uint8_t value)
{
    if(word == NULL || bit_position >= 16U) return;
    if(value != 0U) *word |= (uint16_t)(1U << bit_position);
    else *word &= (uint16_t)~(uint16_t)(1U << bit_position);
}

void SetDiskInitStatusBit(uint8_t bit_position, uint8_t value)
{
    set_bit(&g_tVar.DiskInitStatus, bit_position, value);
}

uint8_t GetDiskInitStatusBit(uint8_t bit_position)
{
    return bit_position < 16U ?
           (uint8_t)((g_tVar.DiskInitStatus >> bit_position) & 1U) : 0U;
}

void SetDevWarningBit(uint16_t *warning, uint8_t bit_position, uint8_t value)
{
    set_bit(warning, bit_position, value);
}

void SetWarningBit(uint8_t bit_position, uint8_t value)
{
    set_bit(&g_tVar.DevWarning, bit_position, value);
}

void SetDiskErrorStatusBit(uint16_t event_addr)
{
    if(event_addr >= 0x1500U && event_addr <= 0x1505U)
        SetWarningBit((uint8_t)(event_addr - 0x1500U + 4U), 1U);
}

void update_disk_alarm_status(uint16_t event_addr)
{
    SetDiskErrorStatusBit(event_addr);
}

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

uint8_t load_realy_state(uint8_t num)
{
    (void)num;
    return g_tVar.RelayState;
}

uint16_t get_temp(uint8_t num)
{
    if(num == 1U) return g_tVar.DS18B20;
    if(num == 2U) return g_tVar.CpuTemp;
    if(num == 3U) return g_tVar.GpuTemp;
    return 0U;
}
