/**
 * @file mcu_uart.h
 * @brief Private STM32H5 UART context and binding interface.
 */

#ifndef MCU_UART_H
#define MCU_UART_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_uart.h"
#include "stm32h5xx_hal.h"

#define MCU_UART_RX_CHUNK_BYTES (64U)
#define MCU_UART_RX_RING_BYTES  (512U)

typedef struct
{
    UART_HandleTypeDef handle;
    DMA_HandleTypeDef receive_dma;
    DMA_HandleTypeDef transmit_dma;
    __ALIGNED(32) uint8_t receive_chunk[MCU_UART_RX_CHUNK_BYTES];
    uint8_t receive_ring[MCU_UART_RX_RING_BYTES];
    uint16_t receive_chunk_bytes;
    uint16_t receive_dma_offset;
    bsp_uart_config_t config;
    volatile uint32_t read_index;
    volatile uint32_t write_index;
    bsp_uart_health_t health;
    volatile bool transmit_complete;
    volatile bool transmit_error;
    bool is_initialized;
} mcu_uart_context_t;

/**
 * Initialize one STM32H5 UART context and ReceiveToIdle buffering.
 * @param context Static UART context owned by one board role.
 * @param instance STM32 UART peripheral instance.
 * @param config Requested baud and ring size.
 * @return BSP status.
 */
bsp_status_t mcu_uart_init(mcu_uart_context_t *context,
                                   USART_TypeDef *instance,
                                   const bsp_uart_config_t *config);
/**
 * Copy received bytes from the STM32H5 UART ring without blocking.
 * @param context Initialized UART context.
 * @param data Destination buffer.
 * @param capacity Destination capacity.
 * @param length Receives copied byte count.
 * @return BSP status.
 */
bsp_status_t mcu_uart_try_read(mcu_uart_context_t *context,
                                       uint8_t *data,
                                       uint32_t capacity,
                                       uint32_t *length);
/**
 * Transmit bytes through an STM32H5 UART with a bounded timeout.
 * @param context Initialized UART context.
 * @param data Transmit buffer.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t mcu_uart_write(mcu_uart_context_t *context,
                                    const uint8_t *data,
                                    uint32_t length,
                                    uint32_t timeout_ms);

/**
 * @brief Copy the normalized runtime line configuration from one UART owner.
 * @param context Initialized UART context.
 * @param config Receives the configuration snapshot.
 * @return BSP status.
 */
bsp_status_t mcu_uart_get_config(
    const mcu_uart_context_t *context,
    bsp_uart_config_t *config);
/**
 * Dispatch a UART interrupt to its owned HAL handle.
 * @param context UART context bound to the active vector.
 * @note ISR context only.
 */
void mcu_uart_irq(mcu_uart_context_t *context);

/**
 * Dispatch a receive DMA interrupt to its owned HAL handle.
 * @param context UART context owning the active GPDMA channel.
 * @note ISR context only.
 */
void mcu_uart_dma_irq(mcu_uart_context_t *context);

/**
 * Dispatch a transmit DMA interrupt to its owned HAL handle.
 * @param context UART context owning the active GPDMA channel.
 * @note ISR context only.
 */
void mcu_uart_tx_dma_irq(mcu_uart_context_t *context);

#endif
