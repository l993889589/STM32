/**
 * @file bsp_timebase.h
 * @brief Optional 1 ms observer for the BSP-owned HAL time base.
 */

#ifndef BSP_TIMEBASE_H
#define BSP_TIMEBASE_H

/** @brief Suspend the BSP-owned HAL millisecond tick interrupt. */
void bsp_timebase_suspend(void);

/** @brief Resume the BSP-owned HAL millisecond tick interrupt. */
void bsp_timebase_resume(void);

#endif /* BSP_TIMEBASE_H */
