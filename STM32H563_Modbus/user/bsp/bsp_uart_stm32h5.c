/**
 * @file bsp_uart_stm32h5.c
 * @brief STM32H5 interrupt/DMA UART implementation and callback routing.
 */

#include "bsp_uart_stm32h5.h"

#include <string.h>
#include "bsp_critical.h"

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
 * @brief Restart bounded ReceiveToIdle interrupt reception for one UART context.
 */
static bsp_status_t bsp_uart_restart_receive(bsp_uart_stm32h5_context_t *context)
{
    HAL_StatusTypeDef hal_status;

    hal_status = HAL_UARTEx_ReceiveToIdle_IT(&context->handle,
                                             context->receive_chunk,
                                             context->receive_chunk_bytes);
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

    context->handle.Instance = instance;
    context->handle.Init.BaudRate = config->baud_rate;
    context->handle.Init.WordLength = UART_WORDLENGTH_8B;
    context->handle.Init.StopBits = UART_STOPBITS_1;
    context->handle.Init.Parity = UART_PARITY_NONE;
    context->handle.Init.Mode = UART_MODE_TX_RX;
    context->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    context->handle.Init.OverSampling = UART_OVERSAMPLING_16;
    context->handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    context->handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    context->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    context->receive_chunk_bytes = (uint16_t)config->receive_chunk_bytes;

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

    status = bsp_uart_register(context);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    context->is_initialized = true;
    return bsp_uart_restart_receive(context);
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

/**
 * @brief Route a HAL ReceiveToIdle event to the registered UART owner.
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    bsp_uart_stm32h5_context_t *context = bsp_uart_find(handle);

    if(context == NULL)
    {
        return;
    }
    if(size > context->receive_chunk_bytes)
    {
        size = context->receive_chunk_bytes;
    }

    context->diagnostics.rx_events++;
    bsp_uart_ring_write(context, context->receive_chunk, size);
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
        (void)bsp_uart_restart_receive(context);
    }
}
