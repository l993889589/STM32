/**
 * @file bsp_uart_stm32h5.c
 * @brief STM32H5 interrupt/DMA UART implementation and callback routing.
 */

#include "bsp_uart_stm32h5.h"

#include <string.h>
#include "bsp_critical.h"
#include "dcache.h"

#define BSP_UART_STM32H5_CONTEXT_COUNT (4U)

static bsp_uart_stm32h5_context_t *bsp_uart_contexts[BSP_UART_STM32H5_CONTEXT_COUNT];

/**
 * @brief Resolve a HAL UART handle to its statically registered BSP owner.
 */
static bsp_uart_stm32h5_context_t *bsp_uart_find(UART_HandleTypeDef *handle)
{
    uint32_t index;

    for(index = 0U; index < BSP_UART_STM32H5_CONTEXT_COUNT; index++)
    {
        if((bsp_uart_contexts[index] != NULL) &&
           (&bsp_uart_contexts[index]->handle == handle))
        {
            return bsp_uart_contexts[index];
        }
    }
    return NULL;
}

/**
 * @brief Register one static UART context for centralized HAL callback routing.
 */
static bsp_status_t bsp_uart_register(bsp_uart_stm32h5_context_t *context)
{
    uint32_t index;

    for(index = 0U; index < BSP_UART_STM32H5_CONTEXT_COUNT; index++)
    {
        if(bsp_uart_contexts[index] == context)
        {
            return BSP_STATUS_ALREADY_INITIALIZED;
        }
        if(bsp_uart_contexts[index] == NULL)
        {
            bsp_uart_contexts[index] = context;
            return BSP_STATUS_OK;
        }
    }
    return BSP_STATUS_OVERFLOW;
}

/**
 * @brief Configure the statically owned USART2 receive DMA channel.
 */
static bsp_status_t bsp_uart_receive_dma_init(bsp_uart_stm32h5_context_t *context)
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
 * @brief Restart bounded ReceiveToIdle reception for one UART context.
 */
static bsp_status_t bsp_uart_restart_receive(bsp_uart_stm32h5_context_t *context)
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

    context->diagnostics.restarts++;
    return BSP_STATUS_OK;
}

/**
 * @brief Move a bounded receive fragment into the static UART ring and record overflow.
 */
static void bsp_uart_ring_write(bsp_uart_stm32h5_context_t *context,
                                const uint8_t *data,
                                uint32_t length)
{
    uint32_t index;

    for(index = 0U; index < length; index++)
    {
        const uint32_t next = (context->write_index + 1U) %
                              BSP_UART_STM32H5_RX_RING_BYTES;
        if(next == context->read_index)
        {
            context->diagnostics.rx_overflow++;
            break;
        }
        context->receive_ring[context->write_index] = data[index];
        context->write_index = next;
        context->diagnostics.rx_bytes++;
    }
}

/**
 * @brief Implement bsp_uart_stm32h5_init() as documented by its interface contract.
 */
bsp_status_t bsp_uart_stm32h5_init(bsp_uart_stm32h5_context_t *context,
                                   USART_TypeDef *instance,
                                   const bsp_uart_config_t *config)
{
    bsp_uart_config_t normalized;
    bsp_status_t status;

    if((context == NULL) || (instance == NULL) || (config == NULL) ||
       (config->baud_rate == 0U) ||
       (config->receive_chunk_bytes == 0U) ||
       (config->receive_chunk_bytes > BSP_UART_STM32H5_RX_CHUNK_BYTES))
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
       (normalized.rx_mode > BSP_UART_RX_MODE_DMA))
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
        status = bsp_uart_receive_dma_init(context);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
    }

    status = bsp_uart_register(context);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    context->is_initialized = true;
    return bsp_uart_restart_receive(context);
}

/** @brief Implement the normalized UART configuration query. */
bsp_status_t bsp_uart_stm32h5_get_config(
    const bsp_uart_stm32h5_context_t *context,
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
 * @brief Implement bsp_uart_stm32h5_try_read() as documented by its interface contract.
 */
bsp_status_t bsp_uart_stm32h5_try_read(bsp_uart_stm32h5_context_t *context,
                                       uint8_t *data,
                                       uint32_t capacity,
                                       uint32_t *length)
{
    bsp_critical_state_t critical_state;

    if((context == NULL) || (data == NULL) || (length == NULL) || (capacity == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    *length = 0U;
    critical_state = bsp_critical_enter();
    while((context->read_index != context->write_index) &&
          (*length < capacity) &&
          (*length < BSP_UART_STM32H5_RX_CHUNK_BYTES))
    {
        data[*length] = context->receive_ring[context->read_index];
        context->read_index = (context->read_index + 1U) % BSP_UART_STM32H5_RX_RING_BYTES;
        (*length)++;
    }
    bsp_critical_exit(critical_state);
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_uart_stm32h5_write() as documented by its interface contract.
 */
bsp_status_t bsp_uart_stm32h5_write(bsp_uart_stm32h5_context_t *context,
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
    if(HAL_UART_Transmit(&context->handle, (uint8_t *)data,
                         (uint16_t)length, timeout_ms) != HAL_OK)
    {
        return BSP_STATUS_TIMEOUT;
    }
    context->diagnostics.tx_bytes += length;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_uart_stm32h5_irq() as documented by its interface contract.
 */
void bsp_uart_stm32h5_irq(bsp_uart_stm32h5_context_t *context)
{
    if((context != NULL) && context->is_initialized)
    {
        HAL_UART_IRQHandler(&context->handle);
    }
}

/** @brief Dispatch the statically owned receive DMA interrupt. */
void bsp_uart_stm32h5_dma_irq(bsp_uart_stm32h5_context_t *context)
{
    if((context != NULL) && context->is_initialized &&
       (context->config.rx_mode == BSP_UART_RX_MODE_DMA))
    {
        HAL_DMA_IRQHandler(&context->receive_dma);
    }
}

/**
 * @brief Route a HAL ReceiveToIdle event to the registered UART owner.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    bsp_uart_stm32h5_context_t *context = bsp_uart_find(handle);
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

    context->diagnostics.rx_events++;
    if(event_type == HAL_UART_RXEVENT_IDLE)
    {
        context->diagnostics.rx_idle_events++;
    }
    else if(event_type == HAL_UART_RXEVENT_HT)
    {
        context->diagnostics.rx_half_events++;
    }
    else
    {
        context->diagnostics.rx_complete_events++;
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
        bsp_uart_ring_write(context, &context->receive_chunk[offset], size - offset);
    }

    if((context->config.rx_mode == BSP_UART_RX_MODE_DMA) &&
       (event_type == HAL_UART_RXEVENT_HT))
    {
        context->receive_dma_offset = size;
        return;
    }
    (void)bsp_uart_restart_receive(context);
}

/**
 * @brief Record a HAL UART error and restart bounded reception.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    bsp_uart_stm32h5_context_t *context = bsp_uart_find(handle);

    if(context != NULL)
    {
        context->diagnostics.errors++;
        if(context->config.rx_mode == BSP_UART_RX_MODE_DMA)
        {
            context->diagnostics.dma_errors++;
        }
        (void)HAL_UART_AbortReceive(handle);
        (void)bsp_uart_restart_receive(context);
    }
}
