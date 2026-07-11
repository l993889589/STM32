/**
 * @file bsp_uart.h
 * @brief Logical board UART public interface.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>
#include "bsp_status.h"

typedef enum
{
    BOARD_UART_DEBUG = 0,
    BOARD_UART_WIFI,
    BOARD_UART_RS485_1,
    BOARD_UART_RS485_2,
    BOARD_UART_COUNT
} board_uart_role_t;

/** @brief UART parity mode used by board-independent timing queries. */
typedef enum
{
    BSP_UART_PARITY_NONE = 0,
    BSP_UART_PARITY_EVEN,
    BSP_UART_PARITY_ODD
} bsp_uart_parity_t;

/** @brief Non-blocking receive backend selected for one UART instance. */
typedef enum
{
    BSP_UART_RX_MODE_IT = 0,
    BSP_UART_RX_MODE_DMA
} bsp_uart_rx_mode_t;

/** @brief Bounded transmit backend selected for one UART instance. */
typedef enum
{
    BSP_UART_TX_MODE_POLLING = 0,
    BSP_UART_TX_MODE_DMA
} bsp_uart_tx_mode_t;

typedef struct
{
    uint32_t baud_rate;
    uint32_t receive_chunk_bytes;
    uint8_t data_bits;
    bsp_uart_parity_t parity;
    uint8_t stop_bits;
    bsp_uart_rx_mode_t rx_mode;
    bsp_uart_tx_mode_t tx_mode;
} bsp_uart_config_t;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_events;
    uint32_t rx_overflow;
    uint32_t errors;
    uint32_t restarts;
    uint32_t tx_bytes;
    uint32_t rx_idle_events;
    uint32_t rx_half_events;
    uint32_t rx_complete_events;
    uint32_t dma_errors;
    uint32_t tx_complete_events;
    uint32_t tx_dma_errors;
    uint32_t tx_timeouts;
} bsp_uart_diagnostics_t;

/**
 * Initialize a logical board UART and start IT or DMA non-blocking reception.
 * @param role Logical UART role.
 * @param config Baud rate and static receive-ring size.
 * @return BSP status.
 */
bsp_status_t bsp_uart_init(board_uart_role_t role, const bsp_uart_config_t *config);
/**
 * Copy currently available UART bytes without blocking.
 * @param role Logical UART role.
 * @param data Caller-owned destination buffer.
 * @param capacity Destination capacity in bytes.
 * @param length Receives the number of bytes copied.
 * @return BSP status; zero available bytes is not an error.
 */
bsp_status_t bsp_uart_try_read(board_uart_role_t role,
                               uint8_t *data,
                               uint32_t capacity,
                               uint32_t *length);
/**
 * Write UART bytes with a bounded timeout.
 * @param role Logical UART role.
 * @param data Transmit bytes valid until the call returns.
 * @param length Number of bytes to transmit.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_uart_write(board_uart_role_t role,
                            const uint8_t *data,
                            uint32_t length,
                            uint32_t timeout_ms);
/**
 * Read diagnostic counters for a logical UART.
 * @param role Logical UART role.
 * @param diagnostics Receives a snapshot of counters.
 * @return BSP status.
 */
bsp_status_t bsp_uart_get_diagnostics(board_uart_role_t role,
                                      bsp_uart_diagnostics_t *diagnostics);

/**
 * @brief Return the normalized line configuration currently owned by a UART.
 * @param role Logical UART role.
 * @param config Receives line format plus the active RX and TX backends.
 * @return BSP_STATUS_OK after initialization, otherwise an explicit error.
 */
bsp_status_t bsp_uart_get_config(board_uart_role_t role,
                                 bsp_uart_config_t *config);

#endif
