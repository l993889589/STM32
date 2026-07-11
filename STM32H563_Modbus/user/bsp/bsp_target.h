/**
 * @file bsp_target.h
 * @brief Compile-time target-selection checks.
 */

#ifndef BSP_TARGET_H
#define BSP_TARGET_H

#include "target_config.h"

#if ((BSP_TARGET_BARE_METAL + BSP_TARGET_THREADX) != 1U)
#error "Select exactly one runtime target"
#endif

#if (BSP_BOARD_DSHAN_H563_INDUSTRIAL != 1U)
#error "Select exactly one supported board"
#endif

#if (BSP_MEMORY_PROFILE_STANDALONE != 1U)
#error "The current compile-only bring-up supports only the standalone memory profile"
#endif

#endif
