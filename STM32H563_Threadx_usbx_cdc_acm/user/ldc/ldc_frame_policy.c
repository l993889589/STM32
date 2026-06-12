#include "ldc_frame_policy.h"

uint32_t ldc_modbus_rtu_timeout_ms(uint32_t baudrate)
{
    uint32_t timeout;

    /* Modbus RTU frame boundary is at least 3.5 character times of silence. */
    if(baudrate == 0U)
        return 4U;

    /* Modbus spec allows a fixed timeout above 19200bps. 2ms is conservative for a 1ms tick. */
    if(baudrate > 19200U)
        return 2U;

    /* 11 bits/char * 3.5 chars * 1000 ms/s = 38500 / baudrate. */
    timeout = (38500U + baudrate - 1U) / baudrate;
    if(timeout == 0U)
        timeout = 1U;

    return timeout;
}

void ldc_frame_policy_apply(ldc_t *ldc, const ldc_frame_policy_t *policy)
{
    uint32_t max_len = 0U;
    uint32_t timeout_ms = 0U;
    int delimiter = -1;

    if(!ldc || !policy)
        return;

    switch(policy->type)
    {
    case LDC_FRAME_POLICY_DELIMITER:
        /* Typical for AT/text protocols: commit when delimiter appears. */
        max_len = policy->max_len;
        delimiter = policy->delimiter;
        break;

    case LDC_FRAME_POLICY_LENGTH_TIMEOUT:
        /* Hybrid strategy: max length, delimiter, and idle timeout can all end a frame. */
        max_len = policy->max_len;
        timeout_ms = policy->timeout_ms;
        delimiter = policy->delimiter;
        break;

    case LDC_FRAME_POLICY_MODBUS_RTU:
        /* Modbus RTU uses silent-time framing only; no delimiter byte exists. */
        max_len = policy->max_len;
        timeout_ms = policy->timeout_ms ? policy->timeout_ms : ldc_modbus_rtu_timeout_ms(policy->baudrate);
        delimiter = -1;
        break;

    case LDC_FRAME_POLICY_TIMEOUT:
    default:
        max_len = policy->max_len;
        timeout_ms = policy->timeout_ms;
        delimiter = -1;
        break;
    }

    ldc_set_frame_config(ldc, max_len, timeout_ms, delimiter);
}
