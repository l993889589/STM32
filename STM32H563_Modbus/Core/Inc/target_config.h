/**
 * @file target_config.h
 * @brief Compile-time board, runtime, and memory-profile selection.
 */

#ifndef TARGET_CONFIG_H
#define TARGET_CONFIG_H

#define BSP_TARGET_BARE_METAL             (1U)
#define BSP_TARGET_THREADX                (0U)
#define BSP_BOARD_DSHAN_H563_INDUSTRIAL   (1U)
#define BSP_MEMORY_PROFILE_STANDALONE     (1U)

#endif
