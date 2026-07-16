/**
 * @file bsp_uart.c
 * @brief USART1/USART2 DMA ReceiveToIdle transport and callback routing.
 */

#include "bsp_uart.h"

#include <stdbool.h>

#include "board_config.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_rx;

volatile uint32_t usart1_dma_error_count;
volatile uint32_t usart2_dma_error_count;

typedef struct
{
    bsp_uart_rx_callback_t rx;
    bsp_uart_error_callback_t error;
    void *context;
} uart_callback_slot_t;

static uint8_t usart1_dma_rx_buffer[BOARD_UART_DMA_RX_BUFFER_SIZE];
static uint8_t usart2_dma_rx_buffer[BOARD_UART_DMA_RX_BUFFER_SIZE];
static volatile uint16_t usart1_dma_rx_position;
static volatile uint16_t usart2_dma_rx_position;
static bsp_uart_diagnostics_t uart_diagnostics;
static uart_callback_slot_t uart_callbacks[BSP_UART_COUNT];
static bool uart_initialized;
static bool uart_msp_failed;

/** @brief Start one circular DMA ReceiveToIdle operation. */
static HAL_StatusTypeDef uart_dma_start(UART_HandleTypeDef *uart,
                                        uint8_t *buffer,
                                        volatile uint16_t *position)
{
    HAL_StatusTypeDef status;

    *position = 0U;
    status = HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                          buffer,
                                          BOARD_UART_DMA_RX_BUFFER_SIZE);
    if(status == HAL_OK)
        __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
    return status;
}

/** @brief Convert a HAL circular-DMA event into one or two ordered segments. */
static void uart_dma_forward(UART_HandleTypeDef *uart,
                             uint8_t *buffer,
                             volatile uint16_t *position,
                             uint16_t size)
{
    uint16_t previous = *position;
    uint16_t event_length;
    bsp_uart_port_t port = uart == &huart1 ? BSP_UART_MODBUS : BSP_UART_DWIN;
    bsp_uart_rx_info_t info;
    HAL_UART_RxEventTypeTypeDef hal_event;

    if(size > BOARD_UART_DMA_RX_BUFFER_SIZE)
    {
        HAL_UART_ErrorCallback(uart);
        return;
    }
    hal_event = HAL_UARTEx_GetRxEventType(uart);
    if(size == BOARD_UART_DMA_RX_BUFFER_SIZE && previous == 0U &&
       hal_event == HAL_UART_RXEVENT_IDLE)
        return;

    if(size > previous)
        event_length = size - previous;
    else if(size < previous)
        event_length = (uint16_t)(BOARD_UART_DMA_RX_BUFFER_SIZE - previous + size);
    else
        return;

    info.timestamp_cycles = DWT->CYCCNT;
    info.event_length = event_length;
    info.first_segment = true;
    info.last_segment = size > previous || size == 0U;
    info.event = hal_event == HAL_UART_RXEVENT_IDLE ?
                 BSP_UART_RX_EVENT_IDLE :
                 BSP_UART_RX_EVENT_TRANSFER_COMPLETE;

    if(port == BSP_UART_MODBUS)
    {
        uart_diagnostics.uart1_rx_events++;
        uart_diagnostics.uart1_rx_bytes += event_length;
    }
    else
    {
        uart_diagnostics.uart2_rx_events++;
        uart_diagnostics.uart2_rx_bytes += event_length;
    }

    if(size > previous)
    {
        if(uart_callbacks[port].rx != NULL)
            uart_callbacks[port].rx(&buffer[previous],
                                    size - previous,
                                    &info,
                                    uart_callbacks[port].context);
    }
    else if(size < previous && uart_callbacks[port].rx != NULL)
    {
        uart_callbacks[port].rx(&buffer[previous],
                                BOARD_UART_DMA_RX_BUFFER_SIZE - previous,
                                &info,
                                uart_callbacks[port].context);
        if(size > 0U)
        {
            info.first_segment = false;
            info.last_segment = true;
            uart_callbacks[port].rx(buffer,
                                    size,
                                    &info,
                                    uart_callbacks[port].context);
        }
    }
    *position = size == BOARD_UART_DMA_RX_BUFFER_SIZE ? 0U : size;
}

/** @brief Bind one UART port to its protocol-owned RX and error callbacks. */
bsp_status_t bsp_uart_set_callbacks(bsp_uart_port_t port,
                                    bsp_uart_rx_callback_t rx_callback,
                                    bsp_uart_error_callback_t error_callback,
                                    void *context)
{
    if(port >= BSP_UART_COUNT)
        return BSP_STATUS_INVALID_ARGUMENT;
    uart_callbacks[port].rx = rx_callback;
    uart_callbacks[port].error = error_callback;
    uart_callbacks[port].context = context;
    return BSP_STATUS_OK;
}

/** @brief Initialize USART1/USART2 and their circular RX DMA channels. */
bsp_status_t bsp_uart_init(void)
{
    if(uart_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    HAL_NVIC_SetPriority(BOARD_DWIN_UART_DMA_IRQn,
                         BOARD_IRQ_PRIORITY_COMMUNICATION,
                         0U);
    HAL_NVIC_EnableIRQ(BOARD_DWIN_UART_DMA_IRQn);
    HAL_NVIC_SetPriority(BOARD_MODBUS_UART_DMA_IRQn,
                         BOARD_IRQ_PRIORITY_COMMUNICATION,
                         0U);
    HAL_NVIC_EnableIRQ(BOARD_MODBUS_UART_DMA_IRQn);

    uart_msp_failed = false;
    huart1.Instance = BOARD_MODBUS_UART;
    huart1.Init.BaudRate = BOARD_UART_BAUD_RATE;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart1) != HAL_OK || uart_msp_failed)
        return BSP_STATUS_IO_ERROR;

    huart2.Instance = BOARD_DWIN_UART;
    huart2.Init.BaudRate = BOARD_UART_BAUD_RATE;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart2) != HAL_OK || uart_msp_failed)
        return BSP_STATUS_IO_ERROR;

    uart_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Start circular ReceiveToIdle DMA on both application UARTs. */
bsp_status_t bsp_uart_start_rx(void)
{
    bsp_status_t result = BSP_STATUS_OK;

    if(!uart_initialized)
        return BSP_STATUS_BUSY;
    if(uart_dma_start(&huart1,
                      usart1_dma_rx_buffer,
                      &usart1_dma_rx_position) != HAL_OK)
    {
        uart_diagnostics.uart1_errors++;
        usart1_dma_error_count++;
        result = BSP_STATUS_IO_ERROR;
    }
    if(uart_dma_start(&huart2,
                      usart2_dma_rx_buffer,
                      &usart2_dma_rx_position) != HAL_OK)
    {
        uart_diagnostics.uart2_errors++;
        usart2_dma_error_count++;
        result = BSP_STATUS_IO_ERROR;
    }
    return result;
}

/** @brief Transmit a bounded blocking frame through the selected UART. */
bsp_status_t bsp_uart_write(bsp_uart_port_t port,
                            const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms)
{
    UART_HandleTypeDef *uart;

    if(port >= BSP_UART_COUNT || data == NULL || length == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!uart_initialized)
        return BSP_STATUS_BUSY;
    uart = port == BSP_UART_MODBUS ? &huart1 : &huart2;
    return HAL_UART_Transmit(uart,
                             (uint8_t *)data,
                             length,
                             timeout_ms) == HAL_OK ? BSP_STATUS_OK :
                                                    BSP_STATUS_IO_ERROR;
}

/** @brief Return the active baud rate for a configured UART port. */
uint32_t bsp_uart_baud_rate(bsp_uart_port_t port)
{
    if(port >= BSP_UART_COUNT || !uart_initialized)
        return 0U;
    return port == BSP_UART_MODBUS ? huart1.Init.BaudRate :
                                     huart2.Init.BaudRate;
}

/** @brief Snapshot the current circular RX DMA producer position. */
uint16_t bsp_uart_rx_dma_position(bsp_uart_port_t port)
{
    DMA_HandleTypeDef *dma;
    uint32_t remaining;

    if(port >= BSP_UART_COUNT || !uart_initialized)
        return 0U;
    dma = port == BSP_UART_MODBUS ? huart1.hdmarx : huart2.hdmarx;
    if(dma == NULL || dma->Instance == NULL)
        return 0U;
    remaining = __HAL_DMA_GET_COUNTER(dma);
    if(remaining > BOARD_UART_DMA_RX_BUFFER_SIZE)
        return 0U;
    return (uint16_t)((BOARD_UART_DMA_RX_BUFFER_SIZE - remaining) %
                      BOARD_UART_DMA_RX_BUFFER_SIZE);
}

/** @brief Return the live UART/DMA diagnostic counters. */
const bsp_uart_diagnostics_t *bsp_uart_diagnostics(void)
{
    return &uart_diagnostics;
}

/** @brief Configure UART GPIO, DMA and NVIC resources selected by the board map. */
void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    DMA_HandleTypeDef *dma;
    GPIO_TypeDef *gpio_port;

    if(uart->Instance == BOARD_MODBUS_UART)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = BOARD_MODBUS_UART_GPIO_PINS;
        gpio.Alternate = BOARD_MODBUS_UART_GPIO_AF;
        gpio_port = BOARD_MODBUS_UART_GPIO_PORT;
        dma = &hdma_usart1_rx;
        dma->Instance = BOARD_MODBUS_UART_DMA;
        dma->Init.Channel = BOARD_MODBUS_UART_DMA_CHANNEL;
        HAL_NVIC_SetPriority(USART1_IRQn, BOARD_IRQ_PRIORITY_COMMUNICATION, 0U);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    else if(uart->Instance == BOARD_DWIN_UART)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = BOARD_DWIN_UART_GPIO_PINS;
        gpio.Alternate = BOARD_DWIN_UART_GPIO_AF;
        gpio_port = BOARD_DWIN_UART_GPIO_PORT;
        dma = &hdma_usart2_rx;
        dma->Instance = BOARD_DWIN_UART_DMA;
        dma->Init.Channel = BOARD_DWIN_UART_DMA_CHANNEL;
        HAL_NVIC_SetPriority(USART2_IRQn, BOARD_IRQ_PRIORITY_COMMUNICATION, 0U);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
    else
    {
        uart_msp_failed = true;
        return;
    }

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(gpio_port, &gpio);
    dma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    dma->Init.PeriphInc = DMA_PINC_DISABLE;
    dma->Init.MemInc = DMA_MINC_ENABLE;
    dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    dma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    dma->Init.Mode = DMA_CIRCULAR;
    dma->Init.Priority = DMA_PRIORITY_HIGH;
    dma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(dma) != HAL_OK)
    {
        uart_msp_failed = true;
        return;
    }
    __HAL_LINKDMA(uart, hdmarx, *dma);
}

/** @brief Release UART GPIO, DMA and interrupt resources. */
void HAL_UART_MspDeInit(UART_HandleTypeDef *uart)
{
    if(uart->Instance == BOARD_MODBUS_UART)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_MODBUS_UART_GPIO_PORT,
                        BOARD_MODBUS_UART_GPIO_PINS);
        HAL_NVIC_DisableIRQ(USART1_IRQn);
    }
    else if(uart->Instance == BOARD_DWIN_UART)
    {
        __HAL_RCC_USART2_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_DWIN_UART_GPIO_PORT,
                        BOARD_DWIN_UART_GPIO_PINS);
        HAL_NVIC_DisableIRQ(USART2_IRQn);
    }
    if(uart->hdmarx != NULL)
        HAL_DMA_DeInit(uart->hdmarx);
}

/** @brief Route one HAL ReceiveToIdle event to the owning protocol callback. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    if(uart == &huart1)
        uart_dma_forward(uart,
                         usart1_dma_rx_buffer,
                         &usart1_dma_rx_position,
                         size);
    else if(uart == &huart2)
        uart_dma_forward(uart,
                         usart2_dma_rx_buffer,
                         &usart2_dma_rx_position,
                         size);
}

/** @brief Abort, diagnose and restart one UART after a HAL receive error. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    bsp_uart_port_t port;
    uint8_t *buffer;
    volatile uint16_t *position;
    uint32_t error_code = uart->ErrorCode;

    if(uart == &huart1)
    {
        port = BSP_UART_MODBUS;
        buffer = usart1_dma_rx_buffer;
        position = &usart1_dma_rx_position;
        uart_diagnostics.uart1_errors++;
        usart1_dma_error_count++;
        if((error_code & HAL_UART_ERROR_ORE) != 0U)
            uart_diagnostics.uart1_ore_errors++;
        if((error_code & HAL_UART_ERROR_DMA) != 0U)
            uart_diagnostics.uart1_dma_errors++;
    }
    else if(uart == &huart2)
    {
        port = BSP_UART_DWIN;
        buffer = usart2_dma_rx_buffer;
        position = &usart2_dma_rx_position;
        uart_diagnostics.uart2_errors++;
        usart2_dma_error_count++;
        if((error_code & HAL_UART_ERROR_ORE) != 0U)
            uart_diagnostics.uart2_ore_errors++;
        if((error_code & HAL_UART_ERROR_DMA) != 0U)
            uart_diagnostics.uart2_dma_errors++;
    }
    else
    {
        return;
    }

    if(uart_callbacks[port].error != NULL)
        uart_callbacks[port].error(uart_callbacks[port].context);
    (void)HAL_UART_AbortReceive(uart);
    __HAL_UART_CLEAR_OREFLAG(uart);
    if(uart_dma_start(uart, buffer, position) == HAL_OK)
    {
        if(port == BSP_UART_MODBUS)
            uart_diagnostics.uart1_restarts++;
        else
            uart_diagnostics.uart2_restarts++;
    }
    else if(port == BSP_UART_MODBUS)
    {
        uart_diagnostics.uart1_restart_failures++;
    }
    else
    {
        uart_diagnostics.uart2_restart_failures++;
    }
}
