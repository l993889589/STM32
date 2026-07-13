/**
 * @file bsp_microtime_config.h
 * @brief Select the microsecond timestamp backend used by UART and Modbus RTU.
 *
 * Select exactly one backend. TIM2 is the default so the example does not
 * require the Cortex-M DWT unit. The DWT backend remains available for boards
 * whose general-purpose timers are already allocated.
 */

#ifndef BSP_MICROTIME_CONFIG_H
#define BSP_MICROTIME_CONFIG_H

#define BSP_MICROTIME_BACKEND_DWT   (1U)
#define BSP_MICROTIME_BACKEND_TIM2  (2U)

#ifndef BSP_MICROTIME_BACKEND
#define BSP_MICROTIME_BACKEND BSP_MICROTIME_BACKEND_TIM2
#endif

#if (BSP_MICROTIME_BACKEND != BSP_MICROTIME_BACKEND_DWT) && \
    (BSP_MICROTIME_BACKEND != BSP_MICROTIME_BACKEND_TIM2)
#error "Unsupported BSP microsecond timebase backend"
#endif

#endif /* BSP_MICROTIME_CONFIG_H */
