/**
 * @file bsp_uart.h
 * @brief Logical board UART interface shared by application services.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

/** @brief Logical UART roles independent of STM32 peripheral instances. */
typedef enum
{
    BSP_UART_W800_AT = 0,
    BSP_UART_RS485_1,
    BSP_UART_RS485_2,
    BSP_UART_DEBUG,
    BSP_UART_COUNT
} bsp_uart_port_t;

/** @brief Compatibility alias for the primary RS-485 port. */
#define BSP_UART_RS485 BSP_UART_RS485_1

/** @brief Receive callback executed from the HAL UART callback context. */
typedef void (*bsp_uart_rx_cb_t)(bsp_uart_port_t port,
                                 const uint8_t *data,
                                 uint16_t length,
                                 void *argument);

/** @brief UART health counters maintained by the STM32 mechanism layer. */
typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_events;
    uint32_t rx_errors;
    uint32_t rx_restarts;
    uint32_t rx_cache_errors;
    uint32_t rx_alignment_errors;
    uint32_t rx_restart_errors;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} bsp_uart_health_t;

/**
 * @brief Initialize all logical UART roles from the current board resource table.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_init(void);

/**
 * @brief Query whether one logical UART receives through DMA.
 * @param port Logical UART role.
 * @return One when DMA reception is active for the role, otherwise zero.
 */
uint8_t bsp_uart_rx_uses_dma(bsp_uart_port_t port);

/**
 * @brief Register the owner callback for received UART fragments.
 * @param port Logical UART role.
 * @param callback Callback invoked from interrupt context; NULL disables delivery.
 * @param argument Caller-owned callback argument with static service lifetime.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_register_rx_callback(bsp_uart_port_t port,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument);

/**
 * @brief Start bounded ReceiveToIdle reception into a caller-owned buffer.
 * @param port Logical UART role.
 * @param buffer Receive buffer valid until reception is replaced or stopped.
 * @param length Buffer length in bytes.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_start_rx(bsp_uart_port_t port, uint8_t *buffer, uint16_t length);

/**
 * @brief Start single-byte ReceiveToIdle reception using BSP-owned storage.
 * @param port Logical UART role.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_start_rx_byte(bsp_uart_port_t port);

/**
 * @brief Transmit bytes with a bounded blocking timeout.
 * @param port Logical UART role.
 * @param data Transmit buffer valid until the call returns.
 * @param length Number of bytes to transmit.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return Transmitted byte count on success, otherwise -1.
 */
int bsp_uart_write(bsp_uart_port_t port,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms);

/**
 * @brief Transmit bytes and wait until the final stop bit leaves the peripheral.
 * @param port Logical UART role.
 * @param data Transmit buffer valid until the call returns.
 * @param length Number of bytes to transmit.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return Transmitted byte count on success, otherwise -1.
 */
int bsp_uart_write_wait_complete(bsp_uart_port_t port,
                                 const uint8_t *data,
                                 uint16_t length,
                                 uint32_t timeout_ms);

/**
 * @brief Read the receive-event counter for one logical UART.
 * @param port Logical UART role.
 * @return Event count, or zero for an invalid role.
 */
uint32_t bsp_uart_rx_events(bsp_uart_port_t port);

/**
 * @brief Copy a consistent UART health snapshot.
 * @param port Logical UART role.
 * @param health Destination for health counters.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_get_health(bsp_uart_port_t port, bsp_uart_health_t *health);

/** @brief Arm start-bit wakeup and suspend active reception before Stop mode. */
int bsp_uart_prepare_stop_wakeup(bsp_uart_port_t port);

/** @brief Restore normal UART reception after Stop mode. */
int bsp_uart_resume_after_stop(bsp_uart_port_t port);

/** @brief Consume one latched UART Stop wakeup event. */
uint8_t bsp_uart_take_stop_wakeup_event(bsp_uart_port_t port);

/** @brief Forward one UART peripheral interrupt to the selected BSP port. */
void bsp_uart_irq_from_isr(bsp_uart_port_t port);

/** @brief Forward one UART receive-DMA interrupt to the selected BSP port. */
void bsp_uart_rx_dma_irq_from_isr(bsp_uart_port_t port);

#endif /* BSP_UART_H */
