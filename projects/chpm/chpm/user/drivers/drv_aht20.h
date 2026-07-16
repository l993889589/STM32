#ifndef DRV_AHT20_H
#define DRV_AHT20_H

#include <stdbool.h>
#include <stdint.h>

bool AHT20_Init(void);
bool AHT20_Reset(void);
bool AHT20_StartMeasurement(void);
bool AHT20_ReadStatus(uint8_t *status);
bool AHT20_ReadMeasurement(float *temperature, float *humidity);

#endif
