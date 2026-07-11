/**
 * @file mcu_uart.c
 * @brief STM32H5 interrupt/DMA UART implementation and callback routing.
 */

#include "mcu_uart.h"

#include <string.h>
#include "bsp_irq_lock.h"
#include "dcache.h"

#define MCU_UART_CONTEXT_COUNT (4U)

static mcu_uart_context_t *mcu_uart_contexts[MCU_UART_CONTEXT_COUNT];

/**
 * @brief Resolve a HAL UART handle to its statically registered BSP owner.
 */
static mcu_uart_context_t *mcu_uart_find(UART_HandleTypeDef *handle)
{
    uint32_t index;

    for(index = 0U; index < MCU_UART_CONTEXT_COUNT; index++)
    {
        if((mcu_uart_contexts[index] != NULL) &&
           (&mcu_uart_contexts[index]->handle == handle))
        {
            return mcu_uart_contexts[index];
        }
    }
    return NULL;
}

/**
 * @brief Register one static UART context for centralized HAL callback routing.
 */
static bsp_status_t mcu_uart_register(mcu_uart_context_t *context)
{
    uint32_t index;

    for(index = 0U; index < MCU_UART_CONTEXT_COUNT; index++)
    {
        if(mcu_uart_contexts[index] == context)
        {
            return BSP_STATUS_ALREADY_INITIALIZED;
        }
        if(mcu_uart_contexts[index] == NULL)
        {
            mcu_uart_contexts[index] = context;
            return BSP_STATUS_OK;
        }
    }
    return BSP_STATUS_OVERFLOW;
}

/**
 * @brief Configure the statically owned USART2 receive DMA channel.
 */
static bsp_status_t mcu_uart_receive_dma_init(mcu_uart_context_t *context)
{
    if(context->handle.Instance != USART2)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    __HAL_RCC_GPDMA1_CLK_ENABLE();
    context->receive_dma.Instance = GPDMA1_Channel1;
    context->receive_dma.Init.Request = GPDMA1_REQUEST_USART2_RX;
    context->receive_dma.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    context->receive_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    context->receive_dma.Init.SrcInc = DMA_SINC_FIXED;
    context->receive_dma.Init.DestInc = DMA_DINC_INCREMENTED;
    context->receive_dma.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    context->receive_dma.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    context->receive_dma.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    context->receive_dma.Init.SrcBurstLength = 1U;
    context->receive_dma.Init.DestBurstLength = 1U;
    context->receive_dma.Init.TransferAllocatedPort =
        DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    context->receive_dma.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    context->receive_dma.Init.Mode = DMA_NORMAL;

    if((HAL_DMA_Init(&context->receive_dma) != HAL_OK) ||
       (HAL_DMA_ConfigChannelAttributes(&context->receive_dma,
                                        DMA_CHANNEL_NPRIV) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }
    __HAL_LINKDMA(&context->handle, hdmarx, context->receive_dma);
    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 10U, 0U);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
    return BSP_STATUS_OK;
}

/**
 * @brief Configure the statically owned USART2 transmit DMA channel.
 * @note Channel 2 is reserved by the board resource map for RS485-1 TX.
 */
static bsp_status_t mcu_uart_transmit_dma_init(mcu_uart_context_t *context)
{
    if(context->handle.Instance != USART2)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    __HAL_RCC_GPDMA1_CLK_ENABLE();
    context->transmit_dma.Instance = GPDMA1_Channel2;
    context->transmit_dma.Init.Request = GPDMA1_REQUEST_USART2_TX;
    context->transmit_dma.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    context->transmit_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    context->transmit_dma.Init.SrcInc = DMA_SINC_INCREMENTED;
    context->transmit_dma.Init.DestInc = DMA_DINC_FIXED;
    context->transmit_dma.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    context->transmit_dma.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    context->transmit_dma.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    context->transmit_dma.Init.SrcBurstLength = 1U;
    context->transmit_dma.Init.DestBurstLength = 1U;
    context->transmit_dma.Init.TransferAllocatedPort =
        DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    context->transmit_dma.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    context->transmit_dma.Init.Mode = DMA_NORMAL;

    if((HAL_DMA_Init(&context->transmit_dma) != HAL_OK) ||
       (HAL_DMA_ConfigChannelAttributes(&context->transmit_dma,
                                        DMA_CHANNEL_NPRIV) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }
    __HAL_LINKDMA(&context->handle, hdmatx, context->transmit_dma);
    HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, 10U, 0U);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);
    return BSP_STATUS_OK;
}

/**
 * @brief Restart bounded ReceiveToIdle reception for one UART context.
 */
static bsp_status_t mcu_uart_restart_receive(mcu_uart_context_t *context)
{
    HAL_StatusTypeDef hal_status;

    context->receive_dma_offset = 0U;
    if(context->config.rx_mode == BSP_UART_RX_MODE_DMA)
    {
        hal_status = HAL_UARTEx_ReceiveToIdle_DMA(&context->handle,
                                                  context->receive_chunk,
                                                  context->receive_chunk_bytes);
    }
    else
    {
        hal_status = HAL_UARTEx_ReceiveToIdle_IT(&context->handle,
                                                 context->receive_chunk,
                                                 context->receive_chunk_bytes);
    }
    if(hal_status != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    context->health.restarts++;
    return BSP_STATUS_OK;
}

/**
 * @brief Move a bounded receive fragment into the static UART ring and record overflow.
 */
static void mcu_uart_ring_write(mcu_uart_context_t *context,
                                const uint8_t *data,
                                uint32_t length)
{
    uint32_t index;

    for(index = 0U; index < length; index++)
    {
        const uint32_t next = (context->write_index + 1U) %
                              MCU_UART_RX_RING_BYTES;
        if(next == context->read_index)
        {
            context->health.rx_overflow++;
            break;
        }
        context->receive_ring[context->write_index] = data[index];
        context->write_index = next;
        context->health.rx_bytes++;
    }
}

/**
 * @brief Implement mcu_uart_init() as documented by its interface contract.
 */
bsp_status_t mcu_uart_init(mcu_uart_context_t *context,
                                   USART_TypeDef *instance,
                                   const bsp_uart_config_t *config)
{
    bsp_uart_config_t normalized;
    bsp_status_t status;

    if((context == NULL) || (instance == NULL) || (config == NULL) ||
       (config->baud_rate == 0U) ||
       (config->receive_chunk_bytes == 0U) ||
       (config->receive_chunk_bytes > MCU_UART_RX_CHUNK_BYTES))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    normalized = *config;
    if(normalized.data_bits == 0U)
    {
        normalized.data_bits = 8U;
    }
    if(normalized.stop_bits == 0U)
    {
        normalized.stop_bits = 1U;
    }
    if((normalized.data_bits != 8U) ||
       ((normalized.stop_bits != 1U) && (normalized.stop_bits != 2U)) ||
       (normalized.parity > BSP_UART_PARITY_ODD) ||
       (normalized.rx_mode > BSP_UART_RX_MODE_DMA) ||
       (normalized.tx_mode > BSP_UART_TX_MODE_DMA))
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    context->handle.Instance = instance;
    context->handle.Init.BaudRate = normalized.baud_rate;
    /* STM32 counts the parity bit inside WordLength: 8 data + parity uses 9B. */
    context->handle.Init.WordLength = normalized.parity == BSP_UART_PARITY_NONE ?
                                      UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    context->handle.Init.StopBits = normalized.stop_bits == 2U ?
                                    UART_STOPBITS_2 : UART_STOPBITS_1;
    context->handle.Init.Parity = normalized.parity == BSP_UART_PARITY_EVEN ?
                                  UART_PARITY_EVEN :
                                  (normalized.parity == BSP_UART_PARITY_ODD ?
                                   UART_PARITY_ODD : UART_PARITY_NONE);
    context->handle.Init.Mode = UART_MODE_TX_RX;
    context->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    context->handle.Init.OverSampling = UART_OVERSAMPLING_16;
    context->handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    context->handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    context->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    context->receive_chunk_bytes = (uint16_t)normalized.receive_chunk_bytes;
    context->config = normalized;

    if(HAL_UART_Init(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    if((HAL_UARTEx_SetTxFifoThreshold(&context->handle, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) ||
       (HAL_UARTEx_SetRxFifoThreshold(&context->handle, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) ||
       (HAL_UARTEx_DisableFifoMode(&context->handle) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }

    if(normalized.rx_mode == BSP_UART_RX_MODE_DMA)
    {
        status = mcu_uart_receive_dma_init(context);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
    }
    if(normalized.tx_mode == BSP_UART_TX_MODE_DMA)
    {
        status = mcu_uart_transmit_dma_init(context);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
    }

    status = mcu_uart_register(context);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    context->is_initialized = true;
    return mcu_uart_restart_receive(context);
}

/** @brief Implement the normalized UART configuration query. */
bsp_status_t mcu_uart_get_config(
    const mcu_uart_context_t *context,
    bsp_uart_config_t *config)
{
    if((context == NULL) || (config == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *config = context->config;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement mcu_uart_try_read() as documented by its interface contract.
 */
bsp_status_t mcu_uart_try_read(mcu_uart_context_t *context,
                                       uint8_t *data,
                                       uint32_t capacity,
                                       uint32_t *length)
{
    bsp_irq_state_t irq_state;

    if((context == NULL) || (data == NULL) || (length == NULL) || (capacity == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    *length = 0U;
    irq_state = bsp_irq_lock();
    while((context->read_index != context->write_index) &&
          (*length < capacity) &&
          (*length < MCU_UART_RX_CHUNK_BYTES))
    {
        data[*length] = context->receive_ring[context->read_index];
        context->read_index = (context->read_index + 1U) % MCU_UART_RX_RING_BYTES;
        (*length)++;
    }
    bsp_irq_unlock(irq_state);
    return BSP_STATUS_OK;
}

/**
 * @brief Implement mcu_uart_write() as documented by its interface contract.
 */
bsp_status_t mcu_uart_write(mcu_uart_context_t *context,
                                    const uint8_t *data,
                                    uint32_t length,
                                    uint32_t timeout_ms)
{
    if((context == NULL) || (data == NULL) || (length == 0U) ||
       (length > UINT16_MAX))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(context->config.tx_mode == BSP_UART_TX_MODE_POLLING)
    {
        if(HAL_UART_Transmit(&context->handle, (uint8_t *)data,
                             (uint16_t)length, timeout_ms) != HAL_OK)
        {
            context->health.tx_timeouts++;
            return BSP_STATUS_TIMEOUT;
        }
    }
    else
    {
        const uintptr_t start = (uintptr_t)data;
        const uintptr_t aligned_start = start & ~(uintptr_t)31U;
        const uint32_t clean_length =
            (uint32_t)(((start - aligned_start) + length + 31U) &
                       ~(uintptr_t)31U);
        const uint32_t started_at = HAL_GetTick();

        if(HAL_DCACHE_CleanByAddr(&hdcache1,
                                  (const uint32_t *)aligned_start,
                                  clean_length) != HAL_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        context->transmit_complete = false;
        context->transmit_error = false;
        if(HAL_UART_Transmit_DMA(&context->handle, (uint8_t *)data,
                                 (uint16_t)length) != HAL_OK)
        {
            context->health.tx_dma_errors++;
            return BSP_STATUS_IO_ERROR;
        }
        while(!context->transmit_complete && !context->transmit_error)
        {
            if((uint32_t)(HAL_GetTick() - started_at) >= timeout_ms)
            {
                context->health.tx_timeouts++;
                (void)HAL_UART_AbortTransmit(&context->handle);
                return BSP_STATUS_TIMEOUT;
            }
        }
        if(context->transmit_error)
        {
            context->health.tx_dma_errors++;
            return BSP_STATUS_IO_ERROR;
        }
    }
    context->health.tx_bytes += length;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement mcu_uart_irq() as documented by its interface contract.
 */
void mcu_uart_irq(mcu_uart_context_t *context)
{
    if((context != NULL) && context->is_initialized)
    {
        HAL_UART_IRQHandler(&context->handle);
    }
}

/** @brief Dispatch the statically owned receive DMA interrupt. */
void mcu_uart_dma_irq(mcu_uart_context_t *context)
{
    if((context != NULL) && context->is_initialized &&
       (context->config.rx_mode == BSP_UART_RX_MODE_DMA))
    {
        HAL_DMA_IRQHandler(&context->receive_dma);
    }
}

/** @brief Dispatch the statically owned transmit DMA interrupt. */
void mcu_uart_tx_dma_irq(mcu_uart_context_t *context)
{
    if((context != NULL) && context->is_initialized &&
       (context->config.tx_mode == BSP_UART_TX_MODE_DMA))
    {
        HAL_DMA_IRQHandler(&context->transmit_dma);
    }
}

/** @brief Complete one bounded DMA transmission from ISR context. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);

    if(context != NULL)
    {
        context->health.tx_complete_events++;
        context->transmit_complete = true;
    }
}

/**
 * @brief Route a HAL ReceiveToIdle event to the registered UART owner.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);
    HAL_UART_RxEventTypeTypeDef event_type;
    uint16_t offset;

    if(context == NULL)
    {
        return;
    }
    event_type = HAL_UARTEx_GetRxEventType(handle);
    if(size > context->receive_chunk_bytes)
    {
        size = context->receive_chunk_bytes;
    }

    offset = context->receive_dma_offset;
    if(size < offset)
    {
        offset = 0U;
    }

    context->health.rx_events++;
    if(event_type == HAL_UART_RXEVENT_IDLE)
    {
        context->health.rx_idle_events++;
    }
    else if(event_type == HAL_UART_RXEVENT_HT)
    {
        context->health.rx_half_events++;
    }
    else
    {
        context->health.rx_complete_events++;
    }

    if(size > offset)
    {
        if(context->config.rx_mode == BSP_UART_RX_MODE_DMA)
        {
            const uintptr_t start = (uintptr_t)&context->receive_chunk[offset];
            const uintptr_t aligned_start = start & ~(uintptr_t)31U;
            const uint32_t invalidate_length =
                (uint32_t)(((start - aligned_start) + (size - offset) + 31U) &
                           ~(uintptr_t)31U);
            (void)HAL_DCACHE_InvalidateByAddr(&hdcache1,
                                               (const uint32_t *)aligned_start,
                                               invalidate_length);
        }
        mcu_uart_ring_write(context, &context->receive_chunk[offset], size - offset);
    }

    if((context->config.rx_mode == BSP_UART_RX_MODE_DMA) &&
       (event_type == HAL_UART_RXEVENT_HT))
    {
        context->receive_dma_offset = size;
        return;
    }
    (void)mcu_uart_restart_receive(context);
}

/**
 * @brief Record a HAL UART error and restart bounded reception.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);

    if(context != NULL)
    {
        context->health.errors++;
        if(context->config.tx_mode == BSP_UART_TX_MODE_DMA &&
           !context->transmit_complete)
        {
            context->transmit_error = true;
        }
        if(context->config.rx_mode == BSP_UART_RX_MODE_DMA)
        {
            context->health.dma_errors++;
        }
        (void)HAL_UART_AbortReceive(handle);
        (void)mcu_uart_restart_receive(context);
    }
}
