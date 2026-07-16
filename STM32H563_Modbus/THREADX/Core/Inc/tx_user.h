/**
 * @file tx_user.h
 * @brief ThreadX configuration for the static STM32H563 Modbus example.
 */

#ifndef TX_USER_H
#define TX_USER_H

#define TX_MAX_PRIORITIES             (32U)
#define TX_TIMER_TICKS_PER_SECOND     (1000U)
#define TX_ENABLE_STACK_CHECKING
#define TX_DISABLE_PREEMPTION_THRESHOLD
#define TX_DISABLE_NOTIFY_CALLBACKS
#define TX_NO_FILEX_POINTER

#endif
