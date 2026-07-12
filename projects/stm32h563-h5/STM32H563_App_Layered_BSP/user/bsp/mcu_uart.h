/**
 * @file mcu_uart.h
 * @brief Private STM32H5 UART ownership, DMA, and callback-routing interface.
 */

#ifndef MCU_UART_H
#define MCU_UART_H

#include <stdint.h>

#include "bsp_uart.h"
#include "stm32h5xx_hal.h"

/** @brief Static runtime context owned by exactly one logical board UART role. */
typedef struct
{
    UART_HandleTypeDef handle;
    DMA_HandleTypeDef rx_dma;
    bsp_uart_port_t port;
    uint8_t use_dma;
    uint8_t cache_invalidate;
    uint8_t rx_byte_mode;
    uint8_t is_initialized;
    uint8_t rx_dma_initialized;
    volatile uint8_t stop_wakeup_event;
    uint8_t *rx_buffer;
    uint16_t rx_size;
    uint8_t rx_byte;
    bsp_uart_rx_cb_t rx_callback;
    void *rx_argument;
    bsp_uart_health_t health;
} mcu_uart_context_t;

/**
 * @brief Initialize one context-owned STM32H5 UART handle.
 * @param context Static context owned by the board role.
 * @param port Logical role used when reporting callbacks.
 * @param instance STM32 UART peripheral instance selected by the board.
 * @param baud_rate Requested UART baud rate in bits per second.
 * @param use_dma Nonzero selects ReceiveToIdle DMA; zero selects interrupt reception.
 * @param cache_invalidate Nonzero invalidates D-cache before delivering DMA data.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_init(mcu_uart_context_t *context,
                  bsp_uart_port_t port,
                  USART_TypeDef *instance,
                  uint32_t baud_rate,
                  uint8_t use_dma,
                  uint8_t cache_invalidate);

/**
 * @brief Configure and link a context-owned GPDMA receive channel.
 * @param context Initialized UART context.
 * @param instance GPDMA channel selected by the board binding.
 * @param request GPDMA request selector for the UART receive signal.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_configure_rx_dma(mcu_uart_context_t *context,
                              DMA_Channel_TypeDef *instance,
                              uint32_t request);

/**
 * @brief Query whether the context has an active DMA receive binding.
 * @param context Initialized UART context.
 * @return One when DMA reception is usable, otherwise zero.
 */
uint8_t mcu_uart_rx_uses_dma(const mcu_uart_context_t *context);

/**
 * @brief Register the receive owner callback for one UART context.
 * @param context Initialized UART context.
 * @param callback Callback invoked from interrupt context; NULL disables delivery.
 * @param argument Caller-owned argument with static service lifetime.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_register_rx_callback(mcu_uart_context_t *context,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument);

/**
 * @brief Start ReceiveToIdle reception into caller-owned storage.
 * @param context Initialized UART context.
 * @param buffer Receive buffer valid for the active reception lifetime.
 * @param length Buffer length in bytes.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_start_rx(mcu_uart_context_t *context, uint8_t *buffer, uint16_t length);

/**
 * @brief Start single-byte ReceiveToIdle reception using context-owned storage.
 * @param context Initialized UART context.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_start_rx_byte(mcu_uart_context_t *context);

/**
 * @brief Transmit bytes with a bounded timeout.
 * @param context Initialized UART context.
 * @param data Transmit buffer valid until the call returns.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return Transmitted byte count on success, otherwise -1.
 */
int mcu_uart_write(mcu_uart_context_t *context,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms);

/**
 * @brief Wait until the final UART stop bit has completed transmission.
 * @param context Initialized UART context.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_wait_tx_complete(mcu_uart_context_t *context, uint32_t timeout_ms);

/**
 * @brief Copy UART health counters from one initialized context.
 * @param context Initialized UART context.
 * @param health Destination snapshot.
 * @return Zero on success, otherwise -1.
 */
int mcu_uart_get_health(const mcu_uart_context_t *context, bsp_uart_health_t *health);

/** @brief Suspend receive and arm start-bit wakeup for a Stop-capable UART. */
int mcu_uart_prepare_stop_wakeup(mcu_uart_context_t *context);

/** @brief Leave UART Stop mode and restore the previously owned reception. */
int mcu_uart_resume_after_stop(mcu_uart_context_t *context);

/** @brief Consume the UART wake-event latch set by the HAL ISR callback. */
uint8_t mcu_uart_take_stop_wakeup_event(mcu_uart_context_t *context);

/**
 * @brief Dispatch one UART vector to the context-owned HAL handle.
 * @param context Initialized UART context.
 * @note ISR context only.
 */
void mcu_uart_irq_from_isr(mcu_uart_context_t *context);

/**
 * @brief Dispatch one receive-DMA vector to the context-owned HAL DMA handle.
 * @param context UART context with an initialized receive DMA channel.
 * @note ISR context only.
 */
void mcu_uart_rx_dma_irq_from_isr(mcu_uart_context_t *context);

#endif /* MCU_UART_H */
