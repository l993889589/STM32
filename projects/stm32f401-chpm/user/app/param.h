/**
 * @file param.h
 * @brief Persistent CHPM configuration and live telemetry state.
 */

#ifndef CHPM_PARAM_H
#define CHPM_PARAM_H

#include <stdbool.h>
#include <stdint.h>

#define PARAM_LEGACY_ADDRESS     0x000000UL
#define PARAM_SLOT_A_ADDRESS     0x001000UL
#define PARAM_SLOT_B_ADDRESS     0x002000UL
#define PARAM_LEGACY_VERSION     0x00000101UL

typedef struct
{
    uint8_t Addr485;
    uint8_t mode;
    uint16_t RestartCnt;
    uint16_t pwm_manual;
    uint16_t pwm_auto;
} PARAM_T;

typedef struct
{
    uint8_t mode;
    uint8_t CanBeep;
    uint8_t RelayState;
    uint16_t DiskInitStatus;
    uint16_t DevWarning;
    uint16_t CpuUsage;
    uint16_t CpuTemp;
    uint16_t FanRate;
    uint16_t CpuSpeed;
    uint16_t GpuUsage;
    uint16_t GpuTemp;
    uint16_t GpuSpeed;
    uint16_t RamUsage;
    uint16_t RamAvMemory;
    uint16_t MbVbatV;
    uint16_t DiskUsage[6];
    uint16_t pwm_manual;
    uint16_t pwm_auto;
    uint16_t DS18B20;
    uint16_t AHT20TEMP;
    uint16_t AHT20HUMI;
    uint32_t RestartCnt;
} VAR_T;

typedef enum
{
    VAR_CPU_USAGE,
    VAR_CPU_TEMP,
    VAR_FAN_RATE,
    VAR_CPU_SPEED,
    VAR_GPU_USAGE,
    VAR_GPU_TEMP,
    VAR_GPU_SPEED,
    VAR_RAM_USAGE,
    VAR_RAM_AVAIL_MEMORY,
    VAR_MB_VBAT_VOLTAGE,
    VAR_DISK_USAGE_1,
    VAR_DISK_USAGE_2,
    VAR_DISK_USAGE_3,
    VAR_DISK_USAGE_4,
    VAR_DISK_USAGE_5,
    VAR_DISK_USAGE_6,
    DEV_PWM,
    AHT20TEMP,
    AHT20HUMI,
    DS18B20,
    RESTARTCNT
} VarType;

typedef enum
{
    PARAM_LOAD_SLOT_A = 0,
    PARAM_LOAD_SLOT_B,
    PARAM_LOAD_LEGACY,
    PARAM_LOAD_DEFAULTS,
    PARAM_LOAD_FLASH_ERROR
} param_load_source_t;

/** @brief Result domain for one complete parameter commit transaction. */
typedef enum
{
    PARAM_STORE_STATUS_OK = 0,
    PARAM_STORE_STATUS_INVALID_ARGUMENT,
    PARAM_STORE_STATUS_SPARE_NOT_READY,
    PARAM_STORE_STATUS_FLASH_ERROR
} param_store_status_t;

extern PARAM_T g_tParam;
extern VAR_T g_tVar;

/** @brief Load the newest valid persistent parameter record. */
void ParamLoad(void);

/**
 * @brief Commit a complete candidate after flash verification.
 * @param candidate Validated application parameter snapshot.
 * @return Typed persistence result; g_tParam changes only after success.
 */
param_store_status_t ParamCommit(const PARAM_T *candidate);

/**
 * @brief Copy the durable parameters into the live telemetry mirror.
 * @note The application must hold its shared-state lock around this call.
 */
void ParamPublishRuntime(void);

/** @brief Return true when the inactive journal sector needs background erase. */
bool ParamSpareNeedsErase(void);

/**
 * @brief Erase and verify readiness of the inactive journal sector.
 * @return Typed preparation result.
 */
param_store_status_t ParamPrepareSpare(void);

/** @brief Return the source selected by the most recent parameter load. */
param_load_source_t ParamLoadSource(void);

/** @brief Return the newest durable journal sequence number. */
uint32_t ParamSequence(void);

/** @brief Return a copy of the current parameters. */
PARAM_T get_param(void);

/** @brief Return the current RS485 address. */
uint8_t param_rs485_addr_get(void);

/** @brief Return the current fan mode. */
uint8_t param_fan_mode_get(void);

/** @brief Return the runtime automatic PWM preset. */
uint16_t param_pwm_auto_get(void);

/** @brief Return the runtime manual PWM preset. */
uint16_t param_pwm_manual_get(void);

/** @brief Update one typed live value while the caller holds the state lock. */
uint8_t SetGlobalVar(VarType type, void *value);

/** @brief Set or clear a disk bit while the caller holds the state lock. */
void SetDiskInitStatusBit(uint8_t bit_position, uint8_t value);

/** @brief Read a disk bit while the caller holds the state lock. */
uint8_t GetDiskInitStatusBit(uint8_t bit_position);

/** @brief Set or clear a bit in a caller-owned warning word. */
void SetDevWarningBit(uint16_t *warning, uint8_t bit_position, uint8_t value);

/** @brief Change a global warning while the caller holds the state lock. */
void SetWarningBit(uint8_t bit_position, uint8_t value);

/** @brief Change a disk warning while the caller holds the state lock. */
void SetDiskErrorStatusBit(uint16_t event_addr);

#endif /* CHPM_PARAM_H */
