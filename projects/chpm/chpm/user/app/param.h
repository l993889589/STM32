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

extern PARAM_T g_tParam;
extern VAR_T g_tVar;

void ParamLoad(void);
bool Param_Store(uint8_t addr, uint8_t mode, uint16_t restart_count,
                 uint16_t pwm_manual, uint16_t pwm_auto);
param_load_source_t ParamLoadSource(void);
uint32_t ParamSequence(void);

PARAM_T get_param(void);
PARAM_T ParamGet(void);
void ParamSet(PARAM_T param);
void ParamSetPlus(uint8_t addr, uint8_t mode, uint16_t restart_count);
void ParamStorePlus(void);
void ParamStoreEx(void);

void load_param(void);
void load_base_param(void);
void restroe_factory_param_setting(void);
void param_rs485_addr_set(uint8_t addr);
uint8_t param_rs485_addr_get(void);
void param_fan_mode_set(uint8_t mode);
uint8_t param_fan_mode_get(void);
void param_pwm_set(uint16_t pwm);
uint16_t param_pwm_get(void);
void param_pwm_auto_set(uint16_t pwm);
uint16_t param_pwm_auto_get(void);
void param_pwm_manual_set(uint16_t pwm);
uint16_t param_pwm_manual_get(void);

uint8_t SetGlobalVar(VarType type, void *value);
void SetDiskInitStatusBit(uint8_t bit_position, uint8_t value);
uint8_t GetDiskInitStatusBit(uint8_t bit_position);
void SetDevWarningBit(uint16_t *warning, uint8_t bit_position, uint8_t value);
void SetWarningBit(uint8_t bit_position, uint8_t value);
void SetDiskErrorStatusBit(uint16_t event_addr);
void update_disk_alarm_status(uint16_t event_addr);
uint8_t load_realy_state(uint8_t num);
uint16_t get_temp(uint8_t num);

#endif
