/**
 * @file bsp_platform.h
 * @brief Small compiler and system-control boundary shared by applications.
 */

#ifndef BSP_PLATFORM_H
#define BSP_PLATFORM_H

/** @brief Declare statically allocated storage aligned to one D-Cache line. */
#define BSP_ALIGN_32(declaration) declaration __attribute__((aligned(32)))

/** @brief Request an immediate MCU system reset; this function does not return. */
void bsp_system_reset(void);

#endif /* BSP_PLATFORM_H */
