/**
 * @file mcu_uart.c
 * @brief STM32H5 UART ownership, DMA receive, cache handling, and HAL callback routing.
 */

#include "mcu_uart.h"

#include <stddef.h>
#include <string.h>

#include "bsp_cache.h"

#define MCU_UART_CONTEXT_COUNT (BSP_UART_COUNT)

static mcu_uart_context_t *g_uart_contexts[MCU_UART_CONTEXT_COUNT];

/** @brief Resolve a HAL UART handle to its statically registered owner. */
static mcu_uart_context_t *mcu_uart_find(UART_HandleTypeDef *handle)
{
    for(uint32_t index = 0U; index < MCU_UART_CONTEXT_COUNT; index++)
    {
        if(g_uart_contexts[index] != NULL && &g_uart_contexts[index]->handle == handle)
            return g_uart_contexts[index];
    }

    return NULL;
}

/** @brief Register one static context for centralized HAL callback dispatch. */
static int mcu_uart_register_context(mcu_uart_context_t *context)
{
    for(uint32_t index = 0U; index < MCU_UART_CONTEXT_COUNT; index++)
    {
        if(g_uart_contexts[index] == context)
            return 0;

        if(g_uart_contexts[index] == NULL)
        {
            g_uart_contexts[index] = context;
            return 0;
        }
    }

    return -1;
}

/** @brief Restart the configured ReceiveToIdle operation after an event or error. */
static int mcu_uart_restart_rx(mcu_uart_context_t *context)
{
    HAL_StatusTypeDef status;

    if(context == NULL || context->is_initialized == 0U ||
       context->rx_buffer == NULL || context->rx_size == 0U)
    {
        return -1;
    }

    if(context->use_dma != 0U)
    {
        if(context->rx_dma_initialized == 0U)
            return -1;

        status = HAL_UARTEx_ReceiveToIdle_DMA(&context->handle,
                                             context->rx_buffer,
                                             context->rx_size);
        if(status == HAL_OK && context->handle.hdmarx != NULL)
            __HAL_DMA_DISABLE_IT(context->handle.hdmarx, DMA_IT_HT);
    }
    else
    {
        status = HAL_UARTEx_ReceiveToIdle_IT(&context->handle,
                                            context->rx_buffer,
                                            context->rx_size);
    }

    if(status != HAL_OK)
        return -1;

    context->health.rx_restarts++;
    return 0;
}

/** @brief Implement mcu_uart_init() as documented by its private interface. */
int mcu_uart_init(mcu_uart_context_t *context,
                  bsp_uart_port_t port,
                  USART_TypeDef *instance,
                  uint32_t baud_rate,
                  uint8_t use_dma,
                  uint8_t cache_invalidate)
{
    if(context == NULL || instance == NULL || port >= BSP_UART_COUNT || baud_rate == 0U)
        return -1;

    if(context->is_initialized != 0U)
        return context->handle.Instance == instance ? 0 : -1;

    memset(context, 0, sizeof(*context));
    context->port = port;
    context->use_dma = use_dma != 0U ? 1U : 0U;
    context->cache_invalidate = cache_invalidate != 0U ? 1U : 0U;
    context->handle.Instance = instance;
    context->handle.Init.BaudRate = baud_rate;
    context->handle.Init.WordLength = UART_WORDLENGTH_8B;
    context->handle.Init.StopBits = UART_STOPBITS_1;
    context->handle.Init.Parity = UART_PARITY_NONE;
    context->handle.Init.Mode = UART_MODE_TX_RX;
    context->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    context->handle.Init.OverSampling = UART_OVERSAMPLING_16;
    context->handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    context->handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    context->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if(HAL_UART_Init(&context->handle) != HAL_OK ||
       HAL_UARTEx_SetTxFifoThreshold(&context->handle, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK ||
       HAL_UARTEx_SetRxFifoThreshold(&context->handle, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK ||
       HAL_UARTEx_DisableFifoMode(&context->handle) != HAL_OK)
    {
        return -1;
    }

    if(mcu_uart_register_context(context) != 0)
        return -1;

    context->is_initialized = 1U;
    return 0;
}

/** @brief Implement context-owned receive DMA setup for an initialized UART. */
int mcu_uart_configure_rx_dma(mcu_uart_context_t *context,
                              DMA_Channel_TypeDef *instance,
                              uint32_t request)
{
    if(context == NULL || context->is_initialized == 0U ||
       context->use_dma == 0U || instance == NULL)
    {
        return -1;
    }

    context->rx_dma.Instance = instance;
    context->rx_dma.Init.Request = request;
    context->rx_dma.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    context->rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    context->rx_dma.Init.SrcInc = DMA_SINC_FIXED;
    context->rx_dma.Init.DestInc = DMA_DINC_INCREMENTED;
    context->rx_dma.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    context->rx_dma.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    context->rx_dma.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
    context->rx_dma.Init.SrcBurstLength = 1U;
    context->rx_dma.Init.DestBurstLength = 1U;
    context->rx_dma.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    context->rx_dma.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    context->rx_dma.Init.Mode = DMA_NORMAL;

    if(HAL_DMA_Init(&context->rx_dma) != HAL_OK ||
       HAL_DMA_ConfigChannelAttributes(&context->rx_dma, DMA_CHANNEL_NPRIV) != HAL_OK)
    {
        return -1;
    }

    __HAL_LINKDMA(&context->handle, hdmarx, context->rx_dma);
    context->rx_dma_initialized = 1U;
    return 0;
}

/** @brief Implement mcu_uart_rx_uses_dma() as documented by its private interface. */
uint8_t mcu_uart_rx_uses_dma(const mcu_uart_context_t *context)
{
    if(context == NULL || context->is_initialized == 0U ||
       context->rx_dma_initialized == 0U || context->handle.hdmarx == NULL)
    {
        return 0U;
    }

    return context->use_dma;
}

/** @brief Implement callback registration for one initialized UART context. */
int mcu_uart_register_rx_callback(mcu_uart_context_t *context,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument)
{
    if(context == NULL || context->is_initialized == 0U)
        return -1;

    context->rx_callback = callback;
    context->rx_argument = argument;
    return 0;
}

/** @brief Implement mcu_uart_start_rx() as documented by its private interface. */
int mcu_uart_start_rx(mcu_uart_context_t *context, uint8_t *buffer, uint16_t length)
{
    if(context == NULL || context->is_initialized == 0U || buffer == NULL || length == 0U)
        return -1;

    context->rx_buffer = buffer;
    context->rx_size = length;
    context->rx_byte_mode = 0U;
    return mcu_uart_restart_rx(context);
}

/** @brief Implement mcu_uart_start_rx_byte() as documented by its private interface. */
int mcu_uart_start_rx_byte(mcu_uart_context_t *context)
{
    if(context == NULL || context->is_initialized == 0U)
        return -1;

    context->rx_buffer = &context->rx_byte;
    context->rx_size = 1U;
    context->rx_byte_mode = 1U;
    return mcu_uart_restart_rx(context);
}

/** @brief Implement bounded blocking transmit for one UART context. */
int mcu_uart_write(mcu_uart_context_t *context,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms)
{
    if(context == NULL || context->is_initialized == 0U || data == NULL || length == 0U)
        return -1;

    if(HAL_UART_Transmit(&context->handle, (uint8_t *)data, length, timeout_ms) != HAL_OK)
    {
        context->health.tx_errors++;
        return -1;
    }

    context->health.tx_bytes += length;
    return (int)length;
}

/** @brief Implement final-stop-bit completion wait for one UART context. */
int mcu_uart_wait_tx_complete(mcu_uart_context_t *context, uint32_t timeout_ms)
{
    uint32_t start_ms;

    if(context == NULL || context->is_initialized == 0U)
        return -1;

    start_ms = HAL_GetTick();
    while(__HAL_UART_GET_FLAG(&context->handle, UART_FLAG_TC) == RESET)
    {
        if(timeout_ms != HAL_MAX_DELAY && (HAL_GetTick() - start_ms) > timeout_ms)
        {
            context->health.tx_errors++;
            return -1;
        }
    }

    return 0;
}

/** @brief Implement mcu_uart_get_health() as documented by its private interface. */
int mcu_uart_get_health(const mcu_uart_context_t *context, bsp_uart_health_t *health)
{
    if(context == NULL || context->is_initialized == 0U || health == NULL)
        return -1;

    *health = context->health;
    return 0;
}

/** @brief Stop current reception and select start-bit wakeup on USART1. */
int mcu_uart_prepare_stop_wakeup(mcu_uart_context_t *context)
{
    UART_WakeUpTypeDef wakeup = {0};

    if((context == NULL) || (context->is_initialized == 0U) ||
       (context->handle.Instance != USART1))
    {
        return -1;
    }

    (void)HAL_UART_AbortReceive(&context->handle);
    context->stop_wakeup_event = 0U;
    wakeup.WakeUpEvent = UART_WAKEUP_ON_STARTBIT;
    if(HAL_UARTEx_StopModeWakeUpSourceConfig(&context->handle, wakeup) != HAL_OK)
    {
        return -1;
    }
    __HAL_UART_CLEAR_FLAG(&context->handle, UART_CLEAR_WUF);
    __HAL_UART_ENABLE_IT(&context->handle, UART_IT_WUF);
    return HAL_UARTEx_EnableStopMode(&context->handle) == HAL_OK ? 0 : -1;
}

/** @brief Disable UART Stop mode and restart the owned ReceiveToIdle transfer. */
int mcu_uart_resume_after_stop(mcu_uart_context_t *context)
{
    if((context == NULL) || (context->is_initialized == 0U) ||
       (context->handle.Instance != USART1))
    {
        return -1;
    }

    if(__HAL_UART_GET_FLAG(&context->handle, UART_FLAG_WUF) != RESET)
    {
        context->stop_wakeup_event = 1U;
    }
    __HAL_UART_DISABLE_IT(&context->handle, UART_IT_WUF);
    if(HAL_UARTEx_DisableStopMode(&context->handle) != HAL_OK)
    {
        return -1;
    }
    __HAL_UART_CLEAR_FLAG(&context->handle, UART_CLEAR_WUF);
    return mcu_uart_restart_rx(context);
}

/** @brief Atomically consume one UART Stop-wakeup callback latch. */
uint8_t mcu_uart_take_stop_wakeup_event(mcu_uart_context_t *context)
{
    uint32_t primask;
    uint8_t event;

    if(context == NULL)
    {
        return 0U;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    event = context->stop_wakeup_event;
    context->stop_wakeup_event = 0U;
    if(primask == 0U)
    {
        __enable_irq();
    }
    return event;
}

/** @brief Dispatch one UART vector to its context-owned HAL handle. */
void mcu_uart_irq_from_isr(mcu_uart_context_t *context)
{
    if(context != NULL && context->is_initialized != 0U)
        HAL_UART_IRQHandler(&context->handle);
}

/** @brief Dispatch one receive-DMA vector to its context-owned HAL DMA handle. */
void mcu_uart_rx_dma_irq_from_isr(mcu_uart_context_t *context)
{
    if(context != NULL && context->rx_dma_initialized != 0U)
        HAL_DMA_IRQHandler(&context->rx_dma);
}

/** @brief Route one HAL ReceiveToIdle event to its registered UART owner. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);

    if(context == NULL)
        return;

    if(size > context->rx_size)
        size = context->rx_size;

    context->health.rx_events++;
    context->health.rx_bytes += size;

    if(size != 0U && context->rx_buffer != NULL)
    {
        if(context->cache_invalidate != 0U)
        {
            (void)bsp_cache_invalidate(context->rx_buffer,
                                       (uint32_t)((size + 31U) & ~31U));
        }

        if(context->rx_callback != NULL)
        {
            context->rx_callback(context->port,
                                 context->rx_buffer,
                                 size,
                                 context->rx_argument);
        }
    }

    if(mcu_uart_restart_rx(context) != 0)
        context->health.rx_errors++;
}

/** @brief Record a HAL UART error and restart the owned ReceiveToIdle operation. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);

    if(context == NULL)
        return;

    context->health.rx_errors++;
    if(mcu_uart_restart_rx(context) != 0)
        context->health.rx_errors++;
}

/** @brief Latch a Stop-mode UART wakeup without using RTOS services in the ISR. */
void HAL_UARTEx_WakeupCallback(UART_HandleTypeDef *handle)
{
    mcu_uart_context_t *context = mcu_uart_find(handle);

    if(context != NULL)
    {
        context->stop_wakeup_event = 1U;
    }
}
