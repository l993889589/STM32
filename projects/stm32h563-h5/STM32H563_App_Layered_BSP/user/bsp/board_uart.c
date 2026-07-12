/**
 * @file board_uart.c
 * @brief Logical UART roles and complete STM32H563 board resource bindings.
 */

#include "board_uart.h"

#include <stddef.h>

#include "board_resources.h"
#include "mcu_uart.h"

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
} board_uart_binding_t;

static mcu_uart_context_t g_board_uart_contexts[BSP_UART_COUNT];
static uint8_t g_board_uart_initialized;

static const board_uart_binding_t g_board_uart_bindings[BSP_UART_COUNT] =
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
static void board_uart_configure_pin(GPIO_TypeDef *port,
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
static mcu_uart_context_t *board_uart_get_context(bsp_uart_port_t port)
{
    return port < BSP_UART_COUNT ? &g_board_uart_contexts[port] : NULL;
}

/** @brief Configure kernel clock, GPIO alternate functions, and peripheral clock for one role. */
static int board_uart_configure_hardware(const board_uart_binding_t *binding)
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
            board_uart_configure_pin(BOARD_UART_W800_TX_PORT,
                                     BOARD_UART_W800_TX_PIN,
                                     BOARD_UART_W800_TX_AF,
                                     BOARD_UART_W800_TX_PULL,
                                     BOARD_UART_W800_TX_SPEED);
            board_uart_configure_pin(BOARD_UART_W800_RX_PORT,
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
            board_uart_configure_pin(BOARD_UART_RS485_1_TX_PORT,
                                     BOARD_UART_RS485_1_TX_PIN,
                                     BOARD_UART_RS485_1_TX_AF,
                                     BOARD_UART_RS485_1_TX_PULL,
                                     BOARD_UART_RS485_1_TX_SPEED);
            board_uart_configure_pin(BOARD_UART_RS485_1_RX_PORT,
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
            board_uart_configure_pin(BOARD_UART_RS485_2_TX_PORT,
                                     BOARD_UART_RS485_2_TX_PIN,
                                     BOARD_UART_RS485_2_TX_AF,
                                     BOARD_UART_RS485_2_TX_PULL,
                                     BOARD_UART_RS485_2_TX_SPEED);
            board_uart_configure_pin(BOARD_UART_RS485_2_RX_PORT,
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
            board_uart_configure_pin(BOARD_UART_DEBUG_TX_PORT,
                                     BOARD_UART_DEBUG_TX_PIN,
                                     BOARD_UART_DEBUG_TX_AF,
                                     BOARD_UART_DEBUG_TX_PULL,
                                     BOARD_UART_DEBUG_TX_SPEED);
            board_uart_configure_pin(BOARD_UART_DEBUG_RX_PORT,
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
static void board_uart_enable_interrupts(const board_uart_binding_t *binding)
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
    if(g_board_uart_initialized != 0U)
        return 0;

    for(uint32_t index = 0U;
        index < (sizeof(g_board_uart_bindings) / sizeof(g_board_uart_bindings[0]));
        index++)
    {
        const board_uart_binding_t *binding = &g_board_uart_bindings[index];
        mcu_uart_context_t *context = board_uart_get_context(binding->port);

        if(board_uart_configure_hardware(binding) != 0 ||
           mcu_uart_init(context,
                         binding->port,
                         binding->instance,
                         binding->baud_rate,
                         binding->use_dma,
                         binding->cache_invalidate) != 0)
        {
            return -1;
        }

        if(binding->rx_dma_instance != NULL &&
           mcu_uart_configure_rx_dma(context,
                                     binding->rx_dma_instance,
                                     binding->rx_dma_request) != 0)
        {
            return -1;
        }

        board_uart_enable_interrupts(binding);
    }

    g_board_uart_initialized = 1U;
    return 0;
}

/** @brief Implement bsp_uart_rx_uses_dma() through the role-owned context. */
uint8_t bsp_uart_rx_uses_dma(bsp_uart_port_t port)
{
    return mcu_uart_rx_uses_dma(board_uart_get_context(port));
}

/** @brief Implement receive callback registration through the board role. */
int bsp_uart_register_rx_callback(bsp_uart_port_t port,
                                  bsp_uart_rx_cb_t callback,
                                  void *argument)
{
    return mcu_uart_register_rx_callback(board_uart_get_context(port),
                                         callback,
                                         argument);
}

/** @brief Implement caller-buffer ReceiveToIdle start through the board role. */
int bsp_uart_start_rx(bsp_uart_port_t port, uint8_t *buffer, uint16_t length)
{
    return mcu_uart_start_rx(board_uart_get_context(port), buffer, length);
}

/** @brief Implement byte-wise ReceiveToIdle start through the board role. */
int bsp_uart_start_rx_byte(bsp_uart_port_t port)
{
    return mcu_uart_start_rx_byte(board_uart_get_context(port));
}

/** @brief Arm the board-selected UART role as a Stop wake source. */
int bsp_uart_prepare_stop_wakeup(bsp_uart_port_t port)
{
    return mcu_uart_prepare_stop_wakeup(board_uart_get_context(port));
}

/** @brief Restore the board-selected UART role after Stop mode. */
int bsp_uart_resume_after_stop(bsp_uart_port_t port)
{
    return mcu_uart_resume_after_stop(board_uart_get_context(port));
}

/** @brief Consume one board-selected UART wake event. */
uint8_t bsp_uart_take_stop_wakeup_event(bsp_uart_port_t port)
{
    return mcu_uart_take_stop_wakeup_event(board_uart_get_context(port));
}

/** @brief Implement bounded UART transmit through the board role. */
int bsp_uart_write(bsp_uart_port_t port,
                   const uint8_t *data,
                   uint16_t length,
                   uint32_t timeout_ms)
{
    return mcu_uart_write(board_uart_get_context(port), data, length, timeout_ms);
}

/** @brief Implement transmit plus final-stop-bit completion through the board role. */
int bsp_uart_write_wait_complete(bsp_uart_port_t port,
                                 const uint8_t *data,
                                 uint16_t length,
                                 uint32_t timeout_ms)
{
    mcu_uart_context_t *context = board_uart_get_context(port);
    int written = mcu_uart_write(context, data, length, timeout_ms);

    if(written < 0 || mcu_uart_wait_tx_complete(context, timeout_ms) != 0)
        return -1;

    return written;
}

/** @brief Return the receive-event counter for one logical role. */
uint32_t bsp_uart_rx_events(bsp_uart_port_t port)
{
    bsp_uart_health_t health;

    return mcu_uart_get_health(board_uart_get_context(port), &health) == 0 ?
           health.rx_events : 0U;
}

/** @brief Implement health snapshot retrieval through the board role. */
int bsp_uart_get_health(bsp_uart_port_t port, bsp_uart_health_t *health)
{
    return mcu_uart_get_health(board_uart_get_context(port), health);
}

/** @brief Dispatch one UART vector by logical role without exposing HAL handles. */
void board_uart_irq_from_isr(bsp_uart_port_t port)
{
    mcu_uart_irq_from_isr(board_uart_get_context(port));
}

/** @brief Dispatch one receive-DMA vector by logical role without exposing DMA handles. */
void board_uart_rx_dma_irq_from_isr(bsp_uart_port_t port)
{
    mcu_uart_rx_dma_irq_from_isr(board_uart_get_context(port));
}
