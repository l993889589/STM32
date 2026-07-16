#ifndef AT_MODULE_EC20_H
#define AT_MODULE_EC20_H

#include "at_module.h"

/** @brief Platform hook required to recover a poisoned EC20 AT session safely. */
typedef bool (*at_module_ec20_hard_reset_cb_t)(void *argument);

/** @brief Optional EC20 platform operations passed through at_module_init(). */
typedef struct
{
    at_module_ec20_hard_reset_cb_t hard_reset;
    void *argument;
    uint32_t ready_delay_ms;
} at_module_ec20_platform_t;

extern const at_module_driver_t g_at_module_ec20;

#endif /* AT_MODULE_EC20_H */
