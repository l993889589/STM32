#ifndef DRV_DS18B20_H
#define DRV_DS18B20_H

#include <stdbool.h>
#include <stdint.h>

bool DS18B20_Init(void);
bool DS18B20_StartConversion(void);
bool DS18B20_ReadTemperature(float *temperature);

#endif
