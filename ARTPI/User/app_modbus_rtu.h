#ifndef APP_MODBUS_RTU_H
#define APP_MODBUS_RTU_H

#include "stm32h7xx_hal.h"

#define APP_MODBUS_RTU_UNIT_ID   1U
#define APP_MODBUS_RTU_BAUD_RATE 115200U

HAL_StatusTypeDef app_modbus_rtu_start(void);

#endif
