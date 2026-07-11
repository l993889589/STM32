/**
 * @file target_config.h
 * @brief Compile-time board, runtime, and memory-profile selection.
 */

#ifndef TARGET_CONFIG_H
#define TARGET_CONFIG_H

#ifdef BSP_RUNTIME_THREADX
#define BSP_TARGET_BARE_METAL             (0U)
#define BSP_TARGET_THREADX                (1U)
#else
#define BSP_TARGET_BARE_METAL             (1U)
#define BSP_TARGET_THREADX                (0U)
#endif
#define BSP_BOARD_DSHAN_H563_INDUSTRIAL   (1U)
#define BSP_MEMORY_PROFILE_STANDALONE     (1U)

#ifndef MODBUS_UART_RX_DMA
#define MODBUS_UART_RX_DMA                (0U)
#endif

#endif
