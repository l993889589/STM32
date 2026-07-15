/**
 * @file bsp_uart.c
 * @brief UART initialization, STM32 hardware control, interrupts, and public BSP API.
 */


#include <stdint.h>

#include "bsp_uart.h"
#include "stm32h5xx_hal.h"
#include "bsp_cache.h"

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
} bsp_uart_hw_context_t;

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
int bsp_uart_hw_init(bsp_uart_hw_context_t *context,
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
int bsp_uart_hw_configure_rx_dma(bsp_uart_hw_context_t *context,
                              DMA_Channel_TypeDef *instance,
                              uint32_t request);

/**
 * @brief Query whether the context has an active DMA receive binding.
 * @param context Initialized UART context.
 * @return One when DMA reception is usable, otherwise zero.
 */
uint8_t bsp_uart_hw_rx_uses_dma(const bsp_uart_hw_context_t *context);

/**
 * @brief Register the receive owner callback for one UART context.
 * @param context Initialized UART context.
 * @param callback Callback invoked from interrupt context; NULL disables delivery.
 * @param argument Caller-owned argument with static service lifetime.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_hw_register_rx_callback(bsp_uart_hw_context_t *context,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument);

/**
 * @brief Start ReceiveToIdle reception into caller-owned storage.
 * @param context Initialized UART context.
 * @param buffer Receive buffer valid for the active reception lifetime.
 * @param length Buffer length in bytes.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_hw_start_rx(bsp_uart_hw_context_t *context, uint8_t *buffer, uint16_t length);

/**
 * @brief Start single-byte ReceiveToIdle reception using context-owned storage.
 * @param context Initialized UART context.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_hw_start_rx_byte(bsp_uart_hw_context_t *context);

/** @brief Reinitialize one context at a new baud and restore its RX owner. */
int bsp_uart_hw_set_baud_rate(bsp_uart_hw_context_t *context,
                              uint32_t baud_rate);

/**
 * @brief Transmit bytes with a bounded timeout.
 * @param context Initialized UART context.
 * @param data Transmit buffer valid until the call returns.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return Transmitted byte count on success, otherwise -1.
 */
int bsp_uart_hw_write(bsp_uart_hw_context_t *context,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms);

/**
 * @brief Wait until the final UART stop bit has completed transmission.
 * @param context Initialized UART context.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_hw_wait_tx_complete(bsp_uart_hw_context_t *context, uint32_t timeout_ms);

/**
 * @brief Copy UART health counters from one initialized context.
 * @param context Initialized UART context.
 * @param health Destination snapshot.
 * @return Zero on success, otherwise -1.
 */
int bsp_uart_hw_get_health(const bsp_uart_hw_context_t *context, bsp_uart_health_t *health);

/** @brief Suspend receive and arm start-bit wakeup for a Stop-capable UART. */
int bsp_uart_hw_prepare_stop_wakeup(bsp_uart_hw_context_t *context);

/** @brief Leave UART Stop mode and restore the previously owned reception. */
int bsp_uart_hw_resume_after_stop(bsp_uart_hw_context_t *context);

/** @brief Consume the UART wake-event latch set by the HAL ISR callback. */
uint8_t bsp_uart_hw_take_stop_wakeup_event(bsp_uart_hw_context_t *context);

/**
 * @brief Dispatch one UART vector to the context-owned HAL handle.
 * @param context Initialized UART context.
 * @note ISR context only.
 */
void bsp_uart_hw_irq_from_isr(bsp_uart_hw_context_t *context);

/**
 * @brief Dispatch one receive-DMA vector to the context-owned HAL DMA handle.
 * @param context UART context with an initialized receive DMA channel.
 * @note ISR context only.
 */
void bsp_uart_hw_rx_dma_irq_from_isr(bsp_uart_hw_context_t *context);


#include "bsp_uart.h"

#include <stddef.h>

#include "bsp_config.h"

/** @brief Physical UART, IRQ, and receive-DMA resources for one logical role. */
typedef struct
{
    bsp_uart_port_t port;
    USART_TypeDef *instance;
    uint32_t baud_rate;
    uint8_t use_dma;
    uint8_t cache_invalidate;
    IRQn_Type uart_irq;
    uint32_t uart_irq_priority;
    DMA_Channel_TypeDef *rx_dma_instance;
    uint32_t rx_dma_request;
    IRQn_Type rx_dma_irq;
    uint32_t rx_dma_irq_priority;
} bsp_uart_binding_t;

static bsp_uart_hw_context_t g_bsp_uart_contexts[BSP_UART_COUNT];
static uint8_t g_bsp_uart_initialized;

static const bsp_uart_binding_t g_bsp_uart_bindings[BSP_UART_COUNT] =
{
    [BSP_UART_W800_AT] = {
        BSP_UART_W800_AT,
        BOARD_UART_W800_INSTANCE,
        BOARD_UART_DEFAULT_BAUD_RATE,
        BOARD_UART_W800_RX_DMA_ENABLED,
        BOARD_UART_W800_RX_DMA_CACHE_INVALIDATE,
        BOARD_UART_W800_IRQ,
        BOARD_UART_W800_IRQ_PRIORITY,
        BOARD_UART_W800_RX_DMA_INSTANCE,
        BOARD_UART_W800_RX_DMA_REQUEST,
        BOARD_UART_W800_RX_DMA_IRQ,
        BOARD_UART_W800_RX_DMA_IRQ_PRIORITY
    },
    [BSP_UART_RS485_1] = {
        BSP_UART_RS485_1,
        BOARD_UART_RS485_1_INSTANCE,
        BOARD_UART_DEFAULT_BAUD_RATE,
        BOARD_UART_RS485_1_RX_DMA_ENABLED,
        BOARD_UART_RS485_1_RX_DMA_CACHE_INVALIDATE,
        BOARD_UART_RS485_1_IRQ,
        BOARD_UART_RS485_1_IRQ_PRIORITY,
        BOARD_UART_RS485_1_RX_DMA_INSTANCE,
        BOARD_UART_RS485_1_RX_DMA_REQUEST,
        BOARD_UART_RS485_1_RX_DMA_IRQ,
        BOARD_UART_RS485_1_RX_DMA_IRQ_PRIORITY
    },
    [BSP_UART_RS485_2] = {
        BSP_UART_RS485_2,
        BOARD_UART_RS485_2_INSTANCE,
        BOARD_UART_DEFAULT_BAUD_RATE,
        BOARD_UART_RS485_2_RX_DMA_ENABLED,
        BOARD_UART_RS485_2_RX_DMA_CACHE_INVALIDATE,
        BOARD_UART_RS485_2_IRQ,
        BOARD_UART_RS485_2_IRQ_PRIORITY,
        BOARD_UART_RS485_2_RX_DMA_INSTANCE,
        BOARD_UART_RS485_2_RX_DMA_REQUEST,
        BOARD_UART_RS485_2_RX_DMA_IRQ,
        BOARD_UART_RS485_2_RX_DMA_IRQ_PRIORITY
    },
    [BSP_UART_DEBUG] = {
        BSP_UART_DEBUG,
        BOARD_UART_DEBUG_INSTANCE,
        BOARD_UART_DEFAULT_BAUD_RATE,
        BOARD_UART_DEBUG_RX_DMA_ENABLED,
        BOARD_UART_DEBUG_RX_DMA_CACHE_INVALIDATE,
        BOARD_UART_DEBUG_IRQ,
        BOARD_UART_DEBUG_IRQ_PRIORITY,
        BOARD_UART_DEBUG_RX_DMA_INSTANCE,
        BOARD_UART_DEBUG_RX_DMA_REQUEST,
        BOARD_UART_DEBUG_RX_DMA_IRQ,
        BOARD_UART_DEBUG_RX_DMA_IRQ_PRIORITY
    },
};

/** @brief Configure one board-selected UART signal pin. */
static void bsp_uart_configure_pin(GPIO_TypeDef *port,
                                     uint32_t pin,
                                     uint32_t alternate,
                                     uint32_t pull,
                                     uint32_t speed)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = pull;
    gpio.Speed = speed;
    gpio.Alternate = alternate;
    HAL_GPIO_Init(port, &gpio);
}

/** @brief Resolve one logical role to its statically owned MCU context. */
static bsp_uart_hw_context_t *bsp_uart_get_context(bsp_uart_port_t port)
{
    return port < BSP_UART_COUNT ? &g_bsp_uart_contexts[port] : NULL;
}

/** @brief Configure kernel clock, GPIO alternate functions, and peripheral clock for one role. */
static int bsp_uart_configure_hardware(const bsp_uart_binding_t *binding)
{
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if(binding == NULL)
        return -1;

    switch(binding->port)
    {
        case BSP_UART_W800_AT:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
            peripheral_clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
            if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
                return -1;

            __HAL_RCC_USART1_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            __HAL_RCC_GPDMA1_CLK_ENABLE();
            bsp_uart_configure_pin(BOARD_UART_W800_TX_PORT,
                                     BOARD_UART_W800_TX_PIN,
                                     BOARD_UART_W800_TX_AF,
                                     BOARD_UART_W800_TX_PULL,
                                     BOARD_UART_W800_TX_SPEED);
            bsp_uart_configure_pin(BOARD_UART_W800_RX_PORT,
                                     BOARD_UART_W800_RX_PIN,
                                     BOARD_UART_W800_RX_AF,
                                     BOARD_UART_W800_RX_PULL,
                                     BOARD_UART_W800_RX_SPEED);
            break;

        case BSP_UART_RS485_1:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART2;
            peripheral_clock.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
            if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
                return -1;

            __HAL_RCC_USART2_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            bsp_uart_configure_pin(BOARD_UART_RS485_1_TX_PORT,
                                     BOARD_UART_RS485_1_TX_PIN,
                                     BOARD_UART_RS485_1_TX_AF,
                                     BOARD_UART_RS485_1_TX_PULL,
                                     BOARD_UART_RS485_1_TX_SPEED);
            bsp_uart_configure_pin(BOARD_UART_RS485_1_RX_PORT,
                                     BOARD_UART_RS485_1_RX_PIN,
                                     BOARD_UART_RS485_1_RX_AF,
                                     BOARD_UART_RS485_1_RX_PULL,
                                     BOARD_UART_RS485_1_RX_SPEED);
            break;

        case BSP_UART_RS485_2:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_UART4;
            peripheral_clock.Uart4ClockSelection = RCC_UART4CLKSOURCE_PCLK1;
            if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
                return -1;

            __HAL_RCC_UART4_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            bsp_uart_configure_pin(BOARD_UART_RS485_2_TX_PORT,
                                     BOARD_UART_RS485_2_TX_PIN,
                                     BOARD_UART_RS485_2_TX_AF,
                                     BOARD_UART_RS485_2_TX_PULL,
                                     BOARD_UART_RS485_2_TX_SPEED);
            bsp_uart_configure_pin(BOARD_UART_RS485_2_RX_PORT,
                                     BOARD_UART_RS485_2_RX_PIN,
                                     BOARD_UART_RS485_2_RX_AF,
                                     BOARD_UART_RS485_2_RX_PULL,
                                     BOARD_UART_RS485_2_RX_SPEED);
            break;

        case BSP_UART_DEBUG:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART3;
            peripheral_clock.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
            if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
                return -1;

            __HAL_RCC_USART3_CLK_ENABLE();
            __HAL_RCC_GPIOC_CLK_ENABLE();
            bsp_uart_configure_pin(BOARD_UART_DEBUG_TX_PORT,
                                     BOARD_UART_DEBUG_TX_PIN,
                                     BOARD_UART_DEBUG_TX_AF,
                                     BOARD_UART_DEBUG_TX_PULL,
                                     BOARD_UART_DEBUG_TX_SPEED);
            bsp_uart_configure_pin(BOARD_UART_DEBUG_RX_PORT,
                                     BOARD_UART_DEBUG_RX_PIN,
                                     BOARD_UART_DEBUG_RX_AF,
                                     BOARD_UART_DEBUG_RX_PULL,
                                     BOARD_UART_DEBUG_RX_SPEED);
            break;

        default:
            return -1;
    }

    return 0;
}

/** @brief Configure NVIC ownership after the UART and optional DMA handles are ready. */
static void bsp_uart_enable_interrupts(const bsp_uart_binding_t *binding)
{
    HAL_NVIC_SetPriority(binding->uart_irq, binding->uart_irq_priority, 0U);
    HAL_NVIC_EnableIRQ(binding->uart_irq);

    if(binding->rx_dma_instance != NULL)
    {
        HAL_NVIC_SetPriority(binding->rx_dma_irq, binding->rx_dma_irq_priority, 0U);
        HAL_NVIC_EnableIRQ(binding->rx_dma_irq);
    }
}

/** @brief Initialize every logical role from the current board resource table. */
int bsp_uart_init(void)
{
    if(g_bsp_uart_initialized != 0U)
        return 0;

    for(uint32_t index = 0U;
        index < (sizeof(g_bsp_uart_bindings) / sizeof(g_bsp_uart_bindings[0]));
        index++)
    {
        const bsp_uart_binding_t *binding = &g_bsp_uart_bindings[index];
        bsp_uart_hw_context_t *context = bsp_uart_get_context(binding->port);

        if(bsp_uart_configure_hardware(binding) != 0 ||
           bsp_uart_hw_init(context,
                         binding->port,
                         binding->instance,
                         binding->baud_rate,
                         binding->use_dma,
                         binding->cache_invalidate) != 0)
        {
            return -1;
        }

        if(binding->rx_dma_instance != NULL &&
           bsp_uart_hw_configure_rx_dma(context,
                                     binding->rx_dma_instance,
                                     binding->rx_dma_request) != 0)
        {
            return -1;
        }

        bsp_uart_enable_interrupts(binding);
    }

    g_bsp_uart_initialized = 1U;
    return 0;
}

/** @brief Implement bsp_uart_rx_uses_dma() through the role-owned context. */
uint8_t bsp_uart_rx_uses_dma(bsp_uart_port_t port)
{
    return bsp_uart_hw_rx_uses_dma(bsp_uart_get_context(port));
}

/** @brief Implement receive callback registration through the board role. */
int bsp_uart_register_rx_callback(bsp_uart_port_t port,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument)
{
    return bsp_uart_hw_register_rx_callback(bsp_uart_get_context(port),
                                         callback,
                                         argument);
}

/** @brief Implement caller-buffer ReceiveToIdle start through the board role. */
int bsp_uart_start_rx(bsp_uart_port_t port, uint8_t *buffer, uint16_t length)
{
    return bsp_uart_hw_start_rx(bsp_uart_get_context(port), buffer, length);
}

/** @brief Implement byte-wise ReceiveToIdle start through the board role. */
int bsp_uart_start_rx_byte(bsp_uart_port_t port)
{
    return bsp_uart_hw_start_rx_byte(bsp_uart_get_context(port));
}

/** @brief Reconfigure a board UART while preserving its callback and RX buffer. */
int bsp_uart_set_baud_rate(bsp_uart_port_t port, uint32_t baud_rate)
{
    const bsp_uart_binding_t *binding;
    int status;

    if(port >= BSP_UART_COUNT || baud_rate == 0U)
        return -1;

    binding = &g_bsp_uart_bindings[port];
    HAL_NVIC_DisableIRQ(binding->uart_irq);
    if(binding->rx_dma_instance != NULL)
        HAL_NVIC_DisableIRQ(binding->rx_dma_irq);

    status = bsp_uart_hw_set_baud_rate(bsp_uart_get_context(port), baud_rate);

    HAL_NVIC_ClearPendingIRQ(binding->uart_irq);
    HAL_NVIC_EnableIRQ(binding->uart_irq);
    if(binding->rx_dma_instance != NULL)
    {
        HAL_NVIC_ClearPendingIRQ(binding->rx_dma_irq);
        HAL_NVIC_EnableIRQ(binding->rx_dma_irq);
    }
    return status;
}

/** @brief Return the active baud from the context-owned HAL handle. */
uint32_t bsp_uart_get_baud_rate(bsp_uart_port_t port)
{
    bsp_uart_hw_context_t *context = bsp_uart_get_context(port);

    return (context != NULL && context->is_initialized != 0U) ?
           context->handle.Init.BaudRate : 0U;
}

/** @brief Arm the board-selected UART role as a Stop wake source. */
int bsp_uart_prepare_stop_wakeup(bsp_uart_port_t port)
{
    return bsp_uart_hw_prepare_stop_wakeup(bsp_uart_get_context(port));
}

/** @brief Restore the board-selected UART role after Stop mode. */
int bsp_uart_resume_after_stop(bsp_uart_port_t port)
{
    return bsp_uart_hw_resume_after_stop(bsp_uart_get_context(port));
}

/** @brief Consume one board-selected UART wake event. */
uint8_t bsp_uart_take_stop_wakeup_event(bsp_uart_port_t port)
{
    return bsp_uart_hw_take_stop_wakeup_event(bsp_uart_get_context(port));
}

/** @brief Implement bounded UART transmit through the board role. */
int bsp_uart_write(bsp_uart_port_t port,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms)
{
    return bsp_uart_hw_write(bsp_uart_get_context(port), data, length, timeout_ms);
}

/** @brief Implement transmit plus final-stop-bit completion through the board role. */
int bsp_uart_write_wait_complete(bsp_uart_port_t port,
                                 const uint8_t *data,
                                 uint16_t length,
                                 uint32_t timeout_ms)
{
    bsp_uart_hw_context_t *context = bsp_uart_get_context(port);
    int written = bsp_uart_hw_write(context, data, length, timeout_ms);

    if(written < 0 || bsp_uart_hw_wait_tx_complete(context, timeout_ms) != 0)
        return -1;

    return written;
}

/** @brief Return the receive-event counter for one logical role. */
uint32_t bsp_uart_rx_events(bsp_uart_port_t port)
{
    bsp_uart_health_t health;

    return bsp_uart_hw_get_health(bsp_uart_get_context(port), &health) == 0 ?
           health.rx_events : 0U;
}

/** @brief Implement health snapshot retrieval through the board role. */
int bsp_uart_get_health(bsp_uart_port_t port, bsp_uart_health_t *health)
{
    return bsp_uart_hw_get_health(bsp_uart_get_context(port), health);
}

/** @brief Dispatch one UART vector by logical role without exposing HAL handles. */
void bsp_uart_irq_from_isr(bsp_uart_port_t port)
{
    bsp_uart_hw_irq_from_isr(bsp_uart_get_context(port));
}

/** @brief Dispatch one receive-DMA vector by logical role without exposing DMA handles. */
void bsp_uart_rx_dma_irq_from_isr(bsp_uart_port_t port)
{
    bsp_uart_hw_rx_dma_irq_from_isr(bsp_uart_get_context(port));
}

/* STM32 hardware implementation. */

#include <stddef.h>
#include <string.h>

#include "bsp_cache.h"

#define MCU_UART_CONTEXT_COUNT (BSP_UART_COUNT)

static bsp_uart_hw_context_t *g_uart_contexts[MCU_UART_CONTEXT_COUNT];

/** @brief Resolve a HAL UART handle to its statically registered owner. */
static bsp_uart_hw_context_t *bsp_uart_hw_find(UART_HandleTypeDef *handle)
{
    for(uint32_t index = 0U; index < MCU_UART_CONTEXT_COUNT; index++)
    {
        if(g_uart_contexts[index] != NULL && &g_uart_contexts[index]->handle == handle)
            return g_uart_contexts[index];
    }

    return NULL;
}

/** @brief Register one static context for centralized HAL callback dispatch. */
static int bsp_uart_hw_register_context(bsp_uart_hw_context_t *context)
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
static int bsp_uart_hw_restart_rx(bsp_uart_hw_context_t *context)
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

        /* DMA owns the complete receive buffer until the next Rx event. */
        if((context->cache_invalidate != 0U) &&
           (bsp_cache_invalidate(context->rx_buffer, context->rx_size) != BSP_STATUS_OK))
        {
            context->health.rx_cache_errors++;
            return -1;
        }

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

/** @brief Implement bsp_uart_hw_init() as documented by its private interface. */
int bsp_uart_hw_init(bsp_uart_hw_context_t *context,
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

    if(bsp_uart_hw_register_context(context) != 0)
        return -1;

    context->is_initialized = 1U;
    return 0;
}

/** @brief Implement context-owned receive DMA setup for an initialized UART. */
int bsp_uart_hw_configure_rx_dma(bsp_uart_hw_context_t *context,
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

/** @brief Implement bsp_uart_hw_rx_uses_dma() as documented by its private interface. */
uint8_t bsp_uart_hw_rx_uses_dma(const bsp_uart_hw_context_t *context)
{
    if(context == NULL || context->is_initialized == 0U ||
       context->rx_dma_initialized == 0U || context->handle.hdmarx == NULL)
    {
        return 0U;
    }

    return context->use_dma;
}

/** @brief Implement callback registration for one initialized UART context. */
int bsp_uart_hw_register_rx_callback(bsp_uart_hw_context_t *context,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument)
{
    if(context == NULL || context->is_initialized == 0U)
        return -1;

    context->rx_callback = callback;
    context->rx_argument = argument;
    return 0;
}

/** @brief Implement bsp_uart_hw_start_rx() as documented by its private interface. */
int bsp_uart_hw_start_rx(bsp_uart_hw_context_t *context, uint8_t *buffer, uint16_t length)
{
    if(context == NULL || context->is_initialized == 0U || buffer == NULL || length == 0U)
        return -1;

    if((context->use_dma != 0U) && (context->cache_invalidate != 0U) &&
       (bsp_cache_is_line_aligned(buffer, length) == 0U))
    {
        context->health.rx_alignment_errors++;
        return -1;
    }

    context->rx_buffer = buffer;
    context->rx_size = length;
    context->rx_byte_mode = 0U;
    return bsp_uart_hw_restart_rx(context);
}

/** @brief Implement bsp_uart_hw_start_rx_byte() as documented by its private interface. */
int bsp_uart_hw_start_rx_byte(bsp_uart_hw_context_t *context)
{
    if(context == NULL || context->is_initialized == 0U)
        return -1;

    context->rx_buffer = &context->rx_byte;
    context->rx_size = 1U;
    context->rx_byte_mode = 1U;
    return bsp_uart_hw_restart_rx(context);
}

/** @brief Reinitialize one UART baud rate and restore its active RX operation. */
int bsp_uart_hw_set_baud_rate(bsp_uart_hw_context_t *context,
                              uint32_t baud_rate)
{
    uint8_t restart_receive;

    if(context == NULL || context->is_initialized == 0U || baud_rate == 0U)
        return -1;

    if(context->handle.Init.BaudRate == baud_rate)
        return 0;

    restart_receive = (context->rx_buffer != NULL && context->rx_size != 0U) ? 1U : 0U;
    (void)HAL_UART_AbortReceive(&context->handle);
    context->handle.Init.BaudRate = baud_rate;

    if(HAL_UART_Init(&context->handle) != HAL_OK ||
       HAL_UARTEx_SetTxFifoThreshold(&context->handle,
                                    UART_TXFIFO_THRESHOLD_1_8) != HAL_OK ||
       HAL_UARTEx_SetRxFifoThreshold(&context->handle,
                                    UART_RXFIFO_THRESHOLD_1_8) != HAL_OK ||
       HAL_UARTEx_DisableFifoMode(&context->handle) != HAL_OK)
    {
        return -1;
    }

    __HAL_UART_CLEAR_FLAG(&context->handle,
                          UART_CLEAR_PEF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF | UART_CLEAR_OREF | UART_CLEAR_TCF);
    return (restart_receive != 0U) ? bsp_uart_hw_restart_rx(context) : 0;
}

/** @brief Implement bounded blocking transmit for one UART context. */
int bsp_uart_hw_write(bsp_uart_hw_context_t *context,
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
int bsp_uart_hw_wait_tx_complete(bsp_uart_hw_context_t *context, uint32_t timeout_ms)
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

/** @brief Implement bsp_uart_hw_get_health() as documented by its private interface. */
int bsp_uart_hw_get_health(const bsp_uart_hw_context_t *context, bsp_uart_health_t *health)
{
    if(context == NULL || context->is_initialized == 0U || health == NULL)
        return -1;

    *health = context->health;
    return 0;
}

/** @brief Stop current reception and select start-bit wakeup on USART1. */
int bsp_uart_hw_prepare_stop_wakeup(bsp_uart_hw_context_t *context)
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
int bsp_uart_hw_resume_after_stop(bsp_uart_hw_context_t *context)
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
    return bsp_uart_hw_restart_rx(context);
}

/** @brief Atomically consume one UART Stop-wakeup callback latch. */
uint8_t bsp_uart_hw_take_stop_wakeup_event(bsp_uart_hw_context_t *context)
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
void bsp_uart_hw_irq_from_isr(bsp_uart_hw_context_t *context)
{
    if(context != NULL && context->is_initialized != 0U)
        HAL_UART_IRQHandler(&context->handle);
}

/** @brief Dispatch one receive-DMA vector to its context-owned HAL DMA handle. */
void bsp_uart_hw_rx_dma_irq_from_isr(bsp_uart_hw_context_t *context)
{
    if(context != NULL && context->rx_dma_initialized != 0U)
        HAL_DMA_IRQHandler(&context->rx_dma);
}

/** @brief Route one HAL ReceiveToIdle event to its registered UART owner. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    bsp_uart_hw_context_t *context = bsp_uart_hw_find(handle);

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
            if(bsp_cache_invalidate(context->rx_buffer,
                                    (uint32_t)((size + BSP_CACHE_LINE_SIZE - 1U) &
                                               ~(BSP_CACHE_LINE_SIZE - 1U))) != BSP_STATUS_OK)
            {
                context->health.rx_cache_errors++;
                context->health.rx_errors++;
                size = 0U;
            }
        }

        if(context->rx_callback != NULL)
        {
            context->rx_callback(context->port,
                                 context->rx_buffer,
                                 size,
                                 context->rx_argument);
        }
    }

    if(bsp_uart_hw_restart_rx(context) != 0)
    {
        context->health.rx_restart_errors++;
        context->health.rx_errors++;
    }
}

/** @brief Record a HAL UART error and restart the owned ReceiveToIdle operation. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    bsp_uart_hw_context_t *context = bsp_uart_hw_find(handle);

    if(context == NULL)
        return;

    context->health.rx_errors++;
    if(bsp_uart_hw_restart_rx(context) != 0)
    {
        context->health.rx_restart_errors++;
        context->health.rx_errors++;
    }
}

/** @brief Latch a Stop-mode UART wakeup without using RTOS services in the ISR. */
void HAL_UARTEx_WakeupCallback(UART_HandleTypeDef *handle)
{
    bsp_uart_hw_context_t *context = bsp_uart_hw_find(handle);

    if(context != NULL)
    {
        context->stop_wakeup_event = 1U;
    }
}
