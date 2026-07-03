#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#include <stdbool.h>

#include "ldc_core.h"
#include "tx_api.h"

UINT app_debug_init(void);
bool app_debug_get_stats(ldc_stats_t *stats);

#endif /* APP_DEBUG_H */
