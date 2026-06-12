#ifndef LDC_FRAME_POLICY_H
#define LDC_FRAME_POLICY_H

#include <stdint.h>
#include "ldc_core.h"

typedef enum
{
    LDC_FRAME_POLICY_TIMEOUT = 0,
    LDC_FRAME_POLICY_DELIMITER,
    LDC_FRAME_POLICY_LENGTH_TIMEOUT,
    LDC_FRAME_POLICY_MODBUS_RTU
} ldc_frame_policy_type_t;

typedef struct
{
    ldc_frame_policy_type_t type;
    uint32_t max_len;
    uint32_t timeout_ms;
    int delimiter;
    uint32_t baudrate;
} ldc_frame_policy_t;

uint32_t ldc_modbus_rtu_timeout_ms(uint32_t baudrate);
void ldc_frame_policy_apply(ldc_t *ldc, const ldc_frame_policy_t *policy);

#endif
