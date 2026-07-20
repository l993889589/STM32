/**
 * @file bsp_stop.h
 * @brief Defined fatal-stop path for unrecoverable startup failures.
 */

#ifndef BSP_STOP_H
#define BSP_STOP_H

/** @brief Disable interrupts and remain in a defined fatal-stop loop. */
void bsp_stop_on_error(void);

#endif /* BSP_STOP_H */
