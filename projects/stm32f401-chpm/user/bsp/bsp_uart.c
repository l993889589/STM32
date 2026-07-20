/**
 * @file bsp_uart.c
 * @brief USART1 Modbus and USART2 DWIN ownership for STM32F401CC.
 */

#include "bsp_uart.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsp_dwt.h"
#include "stm32f4xx_hal.h"

#define BSP_UART_BAUD_RATE       (115200UL)
#define BSP_UART_RX_BUFFER_SIZE  (256U)
#define BSP_UART_RX_BOUNDARY     (BSP_UART_RX_BUFFER_SIZE / 2U)
#define BSP_UART_IRQ_PRIORITY    (5U)

/** @brief All resources required by one statically assigned serial link. */
typedef struct
{
    UART_HandleTypeDef uart;
    DMA_HandleTypeDef rx_dma;
    uint8_t rx_buffer[BSP_UART_RX_BUFFER_SIZE];
    bsp_uart_rx_callback_t receive;
    bsp_uart_error_callback_t error;
    void *context;
    bsp_uart_diagnostics_t diagnostics;
    uint16_t last_position;
    uint16_t expected_boundary;
    bool receiving;
} bsp_uart_device_t;

static bsp_uart_device_t g_modbus;
static bsp_uart_device_t g_dwin;
static bool g_initialized;

/** @brief Resolve a public role without exposing the underlying HAL handle. */
static bsp_uart_device_t *bsp_uart_device(bsp_uart_port_t port)
{
    if(port == BSP_UART_MODBUS)
        return &g_modbus;
    if(port == BSP_UART_DWIN)
        return &g_dwin;
    return NULL;
}

/** @brief Configure one UART handle with the board's fixed 8-N-1 contract. */
static void bsp_uart_configure_handle(UART_HandleTypeDef *uart,
                                      USART_TypeDef *instance)
{
    uart->Instance = instance;
    uart->Init.BaudRate = BSP_UART_BAUD_RATE;
    uart->Init.WordLength = UART_WORDLENGTH_8B;
    uart->Init.StopBits = UART_STOPBITS_1;
    uart->Init.Parity = UART_PARITY_NONE;
    uart->Init.Mode = UART_MODE_TX_RX;
    uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart->Init.OverSampling = UART_OVERSAMPLING_16;
}

/** @brief Configure the USART1 pins and its circular RX DMA stream. */
static bsp_status_t bsp_uart_init_modbus(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    bsp_uart_configure_handle(&g_modbus.uart, USART1);
    g_modbus.rx_dma.Instance = DMA2_Stream2;
    g_modbus.rx_dma.Init.Channel = DMA_CHANNEL_4;
    g_modbus.rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_modbus.rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_modbus.rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_modbus.rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_modbus.rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_modbus.rx_dma.Init.Mode = DMA_CIRCULAR;
    g_modbus.rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    g_modbus.rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&g_modbus.rx_dma) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    __HAL_LINKDMA(&g_modbus.uart, hdmarx, g_modbus.rx_dma);
    if(HAL_UART_Init(&g_modbus.uart) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return BSP_STATUS_OK;
}

/** @brief Configure the USART2 pins and its circular RX DMA stream. */
static bsp_status_t bsp_uart_init_dwin(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    bsp_uart_configure_handle(&g_dwin.uart, USART2);
    g_dwin.rx_dma.Instance = DMA1_Stream5;
    g_dwin.rx_dma.Init.Channel = DMA_CHANNEL_4;
    g_dwin.rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_dwin.rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_dwin.rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_dwin.rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_dwin.rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_dwin.rx_dma.Init.Mode = DMA_CIRCULAR;
    g_dwin.rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    g_dwin.rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&g_dwin.rx_dma) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    __HAL_LINKDMA(&g_dwin.uart, hdmarx, g_dwin.rx_dma);
    if(HAL_UART_Init(&g_dwin.uart) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    return BSP_STATUS_OK;
}

/** @brief Start one circular receive-to-idle path. */
static bsp_status_t bsp_uart_start_device(bsp_uart_device_t *device)
{
    if(HAL_UARTEx_ReceiveToIdle_DMA(&device->uart,
                                    device->rx_buffer,
                                    BSP_UART_RX_BUFFER_SIZE) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    device->last_position = 0U;
    device->expected_boundary = BSP_UART_RX_BOUNDARY;
    device->receiving = true;
    return BSP_STATUS_OK;
}

/** @brief Re-synchronize the next mandatory DMA boundary after a gap. */
static void bsp_uart_resynchronize(bsp_uart_device_t *device,
                                   uint16_t position)
{
    device->last_position =
        position == BSP_UART_RX_BUFFER_SIZE ? 0U : position;
    device->expected_boundary =
        position < BSP_UART_RX_BOUNDARY ?
        BSP_UART_RX_BOUNDARY : BSP_UART_RX_BUFFER_SIZE;
    if(position == BSP_UART_RX_BUFFER_SIZE)
        device->expected_boundary = BSP_UART_RX_BOUNDARY;
}

/** @brief Publish one or two contiguous spans from a circular DMA event. */
static void bsp_uart_publish(bsp_uart_device_t *device,
                             uint16_t position,
                             bsp_uart_rx_event_t event)
{
    bsp_uart_rx_info_t info;
    uint16_t event_length;
    uint16_t required_boundary;

    if(position > BSP_UART_RX_BUFFER_SIZE)
        position = BSP_UART_RX_BUFFER_SIZE;
    if(position >= device->last_position)
        event_length = position - device->last_position;
    else
        event_length = (BSP_UART_RX_BUFFER_SIZE - device->last_position) +
                       position;
    required_boundary = event == BSP_UART_RX_EVENT_HALF_TRANSFER ?
                        BSP_UART_RX_BOUNDARY : BSP_UART_RX_BUFFER_SIZE;
    if(event != BSP_UART_RX_EVENT_IDLE &&
       (position != required_boundary ||
        position != device->expected_boundary ||
        event_length == 0U ||
        event_length > BSP_UART_RX_BOUNDARY))
    {
        device->diagnostics.rx_overflows++;
        bsp_uart_resynchronize(device, position);
        if(device->error != NULL)
            device->error(device->context);
        return;
    }
    if(event_length == 0U)
        return;

    info.timestamp_cycles = bsp_dwt_get_cycles();
    info.event_length = event_length;
    info.event = event;
    info.first_segment = 1U;
    info.last_segment = (position >= device->last_position) ? 1U : 0U;
    if(device->receive != NULL)
    {
        if(position >= device->last_position)
        {
            device->receive(&device->rx_buffer[device->last_position],
                            event_length,
                            &info,
                            device->context);
        }
        else
        {
            uint16_t first_length =
                BSP_UART_RX_BUFFER_SIZE - device->last_position;

            device->receive(&device->rx_buffer[device->last_position],
                            first_length,
                            &info,
                            device->context);
            info.first_segment = 0U;
            info.last_segment = 1U;
            if(position > 0U)
            {
                device->receive(device->rx_buffer,
                                position,
                                &info,
                                device->context);
            }
        }
    }
    device->last_position = (position == BSP_UART_RX_BUFFER_SIZE) ? 0U :
                            position;
    if(event == BSP_UART_RX_EVENT_HALF_TRANSFER)
        device->expected_boundary = BSP_UART_RX_BUFFER_SIZE;
    else if(event == BSP_UART_RX_EVENT_FULL_TRANSFER)
        device->expected_boundary = BSP_UART_RX_BOUNDARY;
    device->diagnostics.rx_events++;
    device->diagnostics.rx_bytes += event_length;
}

/** @brief Initialize both product UARTs and their private receive resources. */
bsp_status_t bsp_uart_init(void)
{
    bsp_status_t status;

    if(g_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;
    status = bsp_uart_init_modbus();
    if(status != BSP_STATUS_OK)
        return status;
    status = bsp_uart_init_dwin();
    if(status != BSP_STATUS_OK)
        return status;
    g_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Register callbacks before the BSP starts receive DMA. */
bsp_status_t bsp_uart_set_callbacks(bsp_uart_port_t port,
                                    bsp_uart_rx_callback_t receive,
                                    bsp_uart_error_callback_t error,
                                    void *context)
{
    bsp_uart_device_t *device = bsp_uart_device(port);

    if(device == NULL || receive == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(device->receiving)
        return BSP_STATUS_BUSY;
    device->receive = receive;
    device->error = error;
    device->context = context;
    return BSP_STATUS_OK;
}

/** @brief Start both receive paths after protocol owners are registered. */
bsp_status_t bsp_uart_start_rx(void)
{
    bsp_status_t status;

    if(!g_initialized)
        return BSP_STATUS_NOT_READY;
    if(g_modbus.receiving || g_dwin.receiving)
        return BSP_STATUS_ALREADY_INITIALIZED;
    status = bsp_uart_start_device(&g_modbus);
    if(status != BSP_STATUS_OK)
        return status;
    status = bsp_uart_start_device(&g_dwin);
    if(status != BSP_STATUS_OK)
    {
        (void)HAL_UART_DMAStop(&g_modbus.uart);
        g_modbus.receiving = false;
    }
    return status;
}

/** @brief Transmit a complete message without leaking the HAL handle. */
bsp_status_t bsp_uart_write(bsp_uart_port_t port,
                            const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms)
{
    bsp_uart_device_t *device = bsp_uart_device(port);
    HAL_StatusTypeDef result;

    if(device == NULL || data == NULL || length == 0U || timeout_ms == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_initialized)
        return BSP_STATUS_NOT_READY;
    result = HAL_UART_Transmit(&device->uart,
                               (uint8_t *)(uintptr_t)data,
                               length,
                               timeout_ms);
    if(result == HAL_OK)
    {
        device->diagnostics.tx_bytes += length;
        return BSP_STATUS_OK;
    }
    device->diagnostics.tx_errors++;
    return (result == HAL_TIMEOUT) ? BSP_STATUS_TIMEOUT :
           ((result == HAL_BUSY) ? BSP_STATUS_BUSY : BSP_STATUS_IO_ERROR);
}

/** @brief Return the configured line rate for protocol timing. */
uint32_t bsp_uart_baud_rate(bsp_uart_port_t port)
{
    bsp_uart_device_t *device = bsp_uart_device(port);

    return (device == NULL) ? 0U : device->uart.Init.BaudRate;
}

/** @brief Snapshot the circular DMA producer index. */
uint16_t bsp_uart_rx_dma_position(bsp_uart_port_t port)
{
    bsp_uart_device_t *device = bsp_uart_device(port);

    if(device == NULL || !device->receiving)
        return 0U;
    return BSP_UART_RX_BUFFER_SIZE -
           (uint16_t)__HAL_DMA_GET_COUNTER(&device->rx_dma);
}

/** @brief Copy UART counters while briefly excluding ISR mutation. */
bsp_status_t bsp_uart_get_diagnostics(bsp_uart_port_t port,
                                      bsp_uart_diagnostics_t *diagnostics)
{
    bsp_uart_device_t *device = bsp_uart_device(port);
    uint32_t state;

    if(device == NULL || diagnostics == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    state = __get_PRIMASK();
    __disable_irq();
    *diagnostics = device->diagnostics;
    __set_PRIMASK(state);
    return BSP_STATUS_OK;
}

/** @brief Dispatch receive-to-idle events to the matching private device. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t position)
{
    bsp_uart_device_t *device;
    bsp_uart_rx_event_t event;
    HAL_UART_RxEventTypeTypeDef hal_event;

    if(uart == &g_modbus.uart)
        device = &g_modbus;
    else if(uart == &g_dwin.uart)
        device = &g_dwin;
    else
        return;
    hal_event = HAL_UARTEx_GetRxEventType(uart);
    if(hal_event == HAL_UART_RXEVENT_HT)
        event = BSP_UART_RX_EVENT_HALF_TRANSFER;
    else if(hal_event == HAL_UART_RXEVENT_TC)
        event = BSP_UART_RX_EVENT_FULL_TRANSFER;
    else
        event = BSP_UART_RX_EVENT_IDLE;
    bsp_uart_publish(device, position, event);
}

/** @brief Recover one circular receive path after a HAL UART error. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    bsp_uart_device_t *device;

    if(uart == &g_modbus.uart)
        device = &g_modbus;
    else if(uart == &g_dwin.uart)
        device = &g_dwin;
    else
        return;

    device->diagnostics.rx_errors++;
    device->receiving = false;
    (void)HAL_UART_DMAStop(&device->uart);
    if(device->error != NULL)
        device->error(device->context);
    if(bsp_uart_start_device(device) == BSP_STATUS_OK)
        device->diagnostics.rx_restarts++;
}

/** @brief Own the USART1 vector beside the USART1 resource. */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_modbus.uart);
}

/** @brief Own the USART2 vector beside the USART2 resource. */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_dwin.uart);
}

/** @brief Own the USART1 RX DMA vector beside the DMA stream. */
void DMA2_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_modbus.rx_dma);
}

/** @brief Own the USART2 RX DMA vector beside the DMA stream. */
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_dwin.rx_dma);
}
