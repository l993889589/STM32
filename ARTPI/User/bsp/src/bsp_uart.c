#include "bsp.h"

#include <string.h>

/* Board pin map: change only this block when moving a port to another pin. */
#define BSP_UART1_TX_PORT       GPIOA
#define BSP_UART1_TX_PIN        GPIO_PIN_9
#define BSP_UART1_TX_AF         GPIO_AF7_USART1
#define BSP_UART1_RX_PORT       GPIOA
#define BSP_UART1_RX_PIN        GPIO_PIN_10
#define BSP_UART1_RX_AF         GPIO_AF7_USART1

#define BSP_UART2_TX_PORT       GPIOA
#define BSP_UART2_TX_PIN        GPIO_PIN_2
#define BSP_UART2_TX_AF         GPIO_AF7_USART2
#define BSP_UART2_RX_PORT       GPIOA
#define BSP_UART2_RX_PIN        GPIO_PIN_3
#define BSP_UART2_RX_AF         GPIO_AF7_USART2

#define BSP_UART3_TX_PORT       GPIOB
#define BSP_UART3_TX_PIN        GPIO_PIN_10
#define BSP_UART3_TX_AF         GPIO_AF7_USART3
#define BSP_UART3_RX_PORT       GPIOB
#define BSP_UART3_RX_PIN        GPIO_PIN_11
#define BSP_UART3_RX_AF         GPIO_AF7_USART3

/* ART-Pi onboard ST-Link virtual COM port. */
#define BSP_UART4_TX_PORT       GPIOA
#define BSP_UART4_TX_PIN        GPIO_PIN_0
#define BSP_UART4_TX_AF         GPIO_AF8_UART4
#define BSP_UART4_RX_PORT       GPIOI
#define BSP_UART4_RX_PIN        GPIO_PIN_9
#define BSP_UART4_RX_AF         GPIO_AF8_UART4

#define BSP_UART5_TX_PORT       GPIOC
#define BSP_UART5_TX_PIN        GPIO_PIN_12
#define BSP_UART5_TX_AF         GPIO_AF8_UART5
#define BSP_UART5_RX_PORT       GPIOD
#define BSP_UART5_RX_PIN        GPIO_PIN_2
#define BSP_UART5_RX_AF         GPIO_AF8_UART5

#define BSP_UART6_TX_PORT       GPIOG
#define BSP_UART6_TX_PIN        GPIO_PIN_14
#define BSP_UART6_TX_AF         GPIO_AF7_USART6
#define BSP_UART6_RX_PORT       GPIOC
#define BSP_UART6_RX_PIN        GPIO_PIN_7
#define BSP_UART6_RX_AF         GPIO_AF7_USART6

#define BSP_UART7_TX_PORT       GPIOB
#define BSP_UART7_TX_PIN        GPIO_PIN_4
#define BSP_UART7_TX_AF         GPIO_AF11_UART7
#define BSP_UART7_RX_PORT       GPIOB
#define BSP_UART7_RX_PIN        GPIO_PIN_3
#define BSP_UART7_RX_AF         GPIO_AF11_UART7

#define BSP_UART8_TX_PORT       GPIOJ
#define BSP_UART8_TX_PIN        GPIO_PIN_8
#define BSP_UART8_TX_AF         GPIO_AF8_UART8
#define BSP_UART8_RX_PORT       GPIOJ
#define BSP_UART8_RX_PIN        GPIO_PIN_9
#define BSP_UART8_RX_AF         GPIO_AF8_UART8

#define BSP_UART_IRQ_PRIORITY   5U

#define BSP_UART_TX_IDLE        0U
#define BSP_UART_TX_STARTING    1U
#define BSP_UART_TX_ACTIVE      2U

typedef struct
{
    USART_TypeDef *instance;
    GPIO_TypeDef *tx_port;
    uint16_t tx_pin;
    uint32_t tx_alternate;
    GPIO_TypeDef *rx_port;
    uint16_t rx_pin;
    uint32_t rx_alternate;
    IRQn_Type interrupt_number;
    uint32_t baud_rate;
    UART_HandleTypeDef handle;
    uint8_t *tx_buffer;
    uint16_t tx_buffer_size;
    volatile uint16_t tx_write;
    volatile uint16_t tx_read;
    volatile uint16_t tx_count;
    volatile uint8_t tx_state;
    volatile uint8_t initialized;
    bsp_uart_rx_callback_t rx_callback;
    void *rx_argument;
    bsp_uart_tx_callback_t send_before;
    bsp_uart_tx_callback_t send_complete;
    void *tx_argument;
} bsp_uart_device_t;

#if BSP_UART1_ENABLED
static uint8_t uart1_tx_buffer[BSP_UART1_TX_BUFFER_SIZE];
static bsp_uart_device_t uart1_device =
{
    .instance = USART1,
    .tx_port = BSP_UART1_TX_PORT,
    .tx_pin = BSP_UART1_TX_PIN,
    .tx_alternate = BSP_UART1_TX_AF,
    .rx_port = BSP_UART1_RX_PORT,
    .rx_pin = BSP_UART1_RX_PIN,
    .rx_alternate = BSP_UART1_RX_AF,
    .interrupt_number = USART1_IRQn,
    .baud_rate = BSP_UART1_BAUD_RATE,
    .tx_buffer = uart1_tx_buffer,
    .tx_buffer_size = BSP_UART1_TX_BUFFER_SIZE
};
#endif

#if BSP_UART2_ENABLED
static uint8_t uart2_tx_buffer[BSP_UART2_TX_BUFFER_SIZE];
static bsp_uart_device_t uart2_device =
{
    .instance = USART2,
    .tx_port = BSP_UART2_TX_PORT,
    .tx_pin = BSP_UART2_TX_PIN,
    .tx_alternate = BSP_UART2_TX_AF,
    .rx_port = BSP_UART2_RX_PORT,
    .rx_pin = BSP_UART2_RX_PIN,
    .rx_alternate = BSP_UART2_RX_AF,
    .interrupt_number = USART2_IRQn,
    .baud_rate = BSP_UART2_BAUD_RATE,
    .tx_buffer = uart2_tx_buffer,
    .tx_buffer_size = BSP_UART2_TX_BUFFER_SIZE
};
#endif

#if BSP_UART3_ENABLED
static uint8_t uart3_tx_buffer[BSP_UART3_TX_BUFFER_SIZE];
static bsp_uart_device_t uart3_device =
{
    .instance = USART3,
    .tx_port = BSP_UART3_TX_PORT,
    .tx_pin = BSP_UART3_TX_PIN,
    .tx_alternate = BSP_UART3_TX_AF,
    .rx_port = BSP_UART3_RX_PORT,
    .rx_pin = BSP_UART3_RX_PIN,
    .rx_alternate = BSP_UART3_RX_AF,
    .interrupt_number = USART3_IRQn,
    .baud_rate = BSP_UART3_BAUD_RATE,
    .tx_buffer = uart3_tx_buffer,
    .tx_buffer_size = BSP_UART3_TX_BUFFER_SIZE
};
#endif

#if BSP_UART4_ENABLED
static uint8_t uart4_tx_buffer[BSP_UART4_TX_BUFFER_SIZE];
static bsp_uart_device_t uart4_device =
{
    .instance = UART4,
    .tx_port = BSP_UART4_TX_PORT,
    .tx_pin = BSP_UART4_TX_PIN,
    .tx_alternate = BSP_UART4_TX_AF,
    .rx_port = BSP_UART4_RX_PORT,
    .rx_pin = BSP_UART4_RX_PIN,
    .rx_alternate = BSP_UART4_RX_AF,
    .interrupt_number = UART4_IRQn,
    .baud_rate = BSP_UART4_BAUD_RATE,
    .tx_buffer = uart4_tx_buffer,
    .tx_buffer_size = BSP_UART4_TX_BUFFER_SIZE
};
#endif

#if BSP_UART5_ENABLED
static uint8_t uart5_tx_buffer[BSP_UART5_TX_BUFFER_SIZE];
static bsp_uart_device_t uart5_device =
{
    .instance = UART5,
    .tx_port = BSP_UART5_TX_PORT,
    .tx_pin = BSP_UART5_TX_PIN,
    .tx_alternate = BSP_UART5_TX_AF,
    .rx_port = BSP_UART5_RX_PORT,
    .rx_pin = BSP_UART5_RX_PIN,
    .rx_alternate = BSP_UART5_RX_AF,
    .interrupt_number = UART5_IRQn,
    .baud_rate = BSP_UART5_BAUD_RATE,
    .tx_buffer = uart5_tx_buffer,
    .tx_buffer_size = BSP_UART5_TX_BUFFER_SIZE
};
#endif

#if BSP_UART6_ENABLED
static uint8_t uart6_tx_buffer[BSP_UART6_TX_BUFFER_SIZE];
static bsp_uart_device_t uart6_device =
{
    .instance = USART6,
    .tx_port = BSP_UART6_TX_PORT,
    .tx_pin = BSP_UART6_TX_PIN,
    .tx_alternate = BSP_UART6_TX_AF,
    .rx_port = BSP_UART6_RX_PORT,
    .rx_pin = BSP_UART6_RX_PIN,
    .rx_alternate = BSP_UART6_RX_AF,
    .interrupt_number = USART6_IRQn,
    .baud_rate = BSP_UART6_BAUD_RATE,
    .tx_buffer = uart6_tx_buffer,
    .tx_buffer_size = BSP_UART6_TX_BUFFER_SIZE
};
#endif

#if BSP_UART7_ENABLED
static uint8_t uart7_tx_buffer[BSP_UART7_TX_BUFFER_SIZE];
static bsp_uart_device_t uart7_device =
{
    .instance = UART7,
    .tx_port = BSP_UART7_TX_PORT,
    .tx_pin = BSP_UART7_TX_PIN,
    .tx_alternate = BSP_UART7_TX_AF,
    .rx_port = BSP_UART7_RX_PORT,
    .rx_pin = BSP_UART7_RX_PIN,
    .rx_alternate = BSP_UART7_RX_AF,
    .interrupt_number = UART7_IRQn,
    .baud_rate = BSP_UART7_BAUD_RATE,
    .tx_buffer = uart7_tx_buffer,
    .tx_buffer_size = BSP_UART7_TX_BUFFER_SIZE
};
#endif

#if BSP_UART8_ENABLED
static uint8_t uart8_tx_buffer[BSP_UART8_TX_BUFFER_SIZE];
static bsp_uart_device_t uart8_device =
{
    .instance = UART8,
    .tx_port = BSP_UART8_TX_PORT,
    .tx_pin = BSP_UART8_TX_PIN,
    .tx_alternate = BSP_UART8_TX_AF,
    .rx_port = BSP_UART8_RX_PORT,
    .rx_pin = BSP_UART8_RX_PIN,
    .rx_alternate = BSP_UART8_RX_AF,
    .interrupt_number = UART8_IRQn,
    .baud_rate = BSP_UART8_BAUD_RATE,
    .tx_buffer = uart8_tx_buffer,
    .tx_buffer_size = BSP_UART8_TX_BUFFER_SIZE
};
#endif

static bsp_uart_device_t *const uart_devices[BSP_UART_PORT_COUNT] =
{
#if BSP_UART1_ENABLED
    &uart1_device,
#else
    NULL,
#endif
#if BSP_UART2_ENABLED
    &uart2_device,
#else
    NULL,
#endif
#if BSP_UART3_ENABLED
    &uart3_device,
#else
    NULL,
#endif
#if BSP_UART4_ENABLED
    &uart4_device,
#else
    NULL,
#endif
#if BSP_UART5_ENABLED
    &uart5_device,
#else
    NULL,
#endif
#if BSP_UART6_ENABLED
    &uart6_device,
#else
    NULL,
#endif
#if BSP_UART7_ENABLED
    &uart7_device,
#else
    NULL,
#endif
#if BSP_UART8_ENABLED
    &uart8_device
#else
    NULL
#endif
};

static bsp_uart_device_t *bsp_uart_get_device(bsp_uart_port_t port);
static void bsp_uart_device_init(bsp_uart_device_t *device);
static void bsp_uart_enable_gpio_clock(GPIO_TypeDef *port);
static void bsp_uart_enable_peripheral_clock(USART_TypeDef *instance);
static void bsp_uart_configure_kernel_clock(USART_TypeDef *instance);
static void bsp_uart_irq_handler(bsp_uart_device_t *device);
static uint32_t bsp_uart_enter_critical(void);
static void bsp_uart_exit_critical(uint32_t interrupt_state);

void bsp_uart_init(void)
{
    uint32_t index;

    for (index = 0U; index < (uint32_t)BSP_UART_PORT_COUNT; index++)
    {
        if (uart_devices[index] != NULL)
        {
            bsp_uart_device_init(uart_devices[index]);
        }
    }
}

HAL_StatusTypeDef bsp_uart_receive_start(bsp_uart_port_t port,
                                         bsp_uart_rx_callback_t callback,
                                         void *argument)
{
    bsp_uart_device_t *device = bsp_uart_get_device(port);
    uint32_t interrupt_state;

    if ((device == NULL) || (device->initialized == 0U) || (callback == NULL))
    {
        return HAL_ERROR;
    }

    interrupt_state = bsp_uart_enter_critical();

    if ((device->rx_callback != NULL) &&
        ((device->rx_callback != callback) || (device->rx_argument != argument)))
    {
        bsp_uart_exit_critical(interrupt_state);
        return HAL_BUSY;
    }

    __HAL_UART_DISABLE_IT(&device->handle, UART_IT_RXNE);
    device->rx_callback = callback;
    device->rx_argument = argument;
    SET_BIT(device->instance->RQR, USART_RQR_RXFRQ);
    WRITE_REG(device->instance->ICR,
              USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF);
    __HAL_UART_ENABLE_IT(&device->handle, UART_IT_RXNE);

    bsp_uart_exit_critical(interrupt_state);
    return HAL_OK;
}

HAL_StatusTypeDef bsp_uart_receive_stop(bsp_uart_port_t port)
{
    bsp_uart_device_t *device = bsp_uart_get_device(port);
    uint32_t interrupt_state;

    if ((device == NULL) || (device->initialized == 0U))
    {
        return HAL_ERROR;
    }

    interrupt_state = bsp_uart_enter_critical();
    __HAL_UART_DISABLE_IT(&device->handle, UART_IT_RXNE);
    device->rx_callback = NULL;
    device->rx_argument = NULL;
    SET_BIT(device->instance->RQR, USART_RQR_RXFRQ);
    WRITE_REG(device->instance->ICR,
              USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF);
    bsp_uart_exit_critical(interrupt_state);

    return HAL_OK;
}

HAL_StatusTypeDef bsp_uart_set_tx_callbacks(bsp_uart_port_t port,
                                             bsp_uart_tx_callback_t send_before,
                                             bsp_uart_tx_callback_t send_complete,
                                             void *argument)
{
    bsp_uart_device_t *device = bsp_uart_get_device(port);
    uint32_t interrupt_state;

    if ((device == NULL) || (device->initialized == 0U))
    {
        return HAL_ERROR;
    }

    interrupt_state = bsp_uart_enter_critical();
    if (device->tx_state != BSP_UART_TX_IDLE)
    {
        bsp_uart_exit_critical(interrupt_state);
        return HAL_BUSY;
    }

    device->send_before = send_before;
    device->send_complete = send_complete;
    device->tx_argument = argument;
    bsp_uart_exit_critical(interrupt_state);

    return HAL_OK;
}

size_t bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, size_t length)
{
    bsp_uart_device_t *device = bsp_uart_get_device(port);
    size_t written = 0U;

    if ((device == NULL) || (device->initialized == 0U) ||
        (data == NULL) || (length == 0U))
    {
        return 0U;
    }

    while (written < length)
    {
        uint8_t queued = 0U;
        uint8_t start_transmission = 0U;

        while (queued == 0U)
        {
            uint32_t interrupt_state = bsp_uart_enter_critical();

            if (device->tx_count < device->tx_buffer_size)
            {
                device->tx_buffer[device->tx_write] = data[written];
                device->tx_write++;
                if (device->tx_write >= device->tx_buffer_size)
                {
                    device->tx_write = 0U;
                }
                device->tx_count++;
                queued = 1U;

                if (device->tx_state == BSP_UART_TX_IDLE)
                {
                    device->tx_state = BSP_UART_TX_STARTING;
                    start_transmission = 1U;
                }
                else if (device->tx_state == BSP_UART_TX_ACTIVE)
                {
                    __HAL_UART_ENABLE_IT(&device->handle, UART_IT_TXE);
                }
            }

            bsp_uart_exit_critical(interrupt_state);
        }

        if (start_transmission != 0U)
        {
            uint32_t interrupt_state;

            if (device->send_before != NULL)
            {
                device->send_before(device->tx_argument);
            }

            interrupt_state = bsp_uart_enter_critical();
            WRITE_REG(device->instance->ICR, USART_ICR_TCCF);
            __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TC);
            device->tx_state = BSP_UART_TX_ACTIVE;
            __HAL_UART_ENABLE_IT(&device->handle, UART_IT_TXE);
            bsp_uart_exit_critical(interrupt_state);
        }

        written++;
    }

    return written;
}

size_t bsp_uart_write_string(bsp_uart_port_t port, const char *text)
{
    if (text == NULL)
    {
        return 0U;
    }

    return bsp_uart_write(port, (const uint8_t *)text, strlen(text));
}

uint8_t bsp_uart_tx_empty(bsp_uart_port_t port)
{
    bsp_uart_device_t *device = bsp_uart_get_device(port);
    uint8_t is_empty;
    uint32_t interrupt_state;

    if ((device == NULL) || (device->initialized == 0U))
    {
        return 0U;
    }

    interrupt_state = bsp_uart_enter_critical();
    is_empty = ((device->tx_count == 0U) &&
                (device->tx_state == BSP_UART_TX_IDLE)) ? 1U : 0U;
    bsp_uart_exit_critical(interrupt_state);

    return is_empty;
}

static bsp_uart_device_t *bsp_uart_get_device(bsp_uart_port_t port)
{
    if ((uint32_t)port >= (uint32_t)BSP_UART_PORT_COUNT)
    {
        return NULL;
    }

    return uart_devices[(uint32_t)port];
}

static void bsp_uart_device_init(bsp_uart_device_t *device)
{
    GPIO_InitTypeDef gpio_config = {0};

    bsp_uart_enable_gpio_clock(device->tx_port);
    bsp_uart_enable_gpio_clock(device->rx_port);
    bsp_uart_enable_peripheral_clock(device->instance);
    bsp_uart_configure_kernel_clock(device->instance);

    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_PULLUP;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio_config.Pin = device->tx_pin;
    gpio_config.Alternate = device->tx_alternate;
    HAL_GPIO_Init(device->tx_port, &gpio_config);

    gpio_config.Pin = device->rx_pin;
    gpio_config.Alternate = device->rx_alternate;
    HAL_GPIO_Init(device->rx_port, &gpio_config);

    device->handle.Instance = device->instance;
    device->handle.Init.BaudRate = device->baud_rate;
    device->handle.Init.WordLength = UART_WORDLENGTH_8B;
    device->handle.Init.StopBits = UART_STOPBITS_1;
    device->handle.Init.Parity = UART_PARITY_NONE;
    device->handle.Init.Mode = UART_MODE_TX_RX;
    device->handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    device->handle.Init.OverSampling = UART_OVERSAMPLING_16;
    device->handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    device->handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    device->handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&device->handle) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_SetTxFifoThreshold(&device->handle, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_SetRxFifoThreshold(&device->handle, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_DisableFifoMode(&device->handle) != HAL_OK)
    {
        BSP_ERROR();
    }

    device->tx_write = 0U;
    device->tx_read = 0U;
    device->tx_count = 0U;
    device->tx_state = BSP_UART_TX_IDLE;
    device->rx_callback = NULL;
    device->rx_argument = NULL;
    device->send_before = NULL;
    device->send_complete = NULL;
    device->tx_argument = NULL;

    __HAL_UART_DISABLE_IT(&device->handle, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TXE);
    __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TC);
    WRITE_REG(device->instance->ICR,
              USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF |
              USART_ICR_ORECF | USART_ICR_TCCF);
    SET_BIT(device->instance->RQR, USART_RQR_RXFRQ);

    HAL_NVIC_SetPriority(device->interrupt_number, BSP_UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(device->interrupt_number);
    device->initialized = 1U;
}

static void bsp_uart_enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
    else if (port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
    else if (port == GPIOF)
    {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    }
    else if (port == GPIOG)
    {
        __HAL_RCC_GPIOG_CLK_ENABLE();
    }
    else if (port == GPIOH)
    {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    }
    else if (port == GPIOI)
    {
        __HAL_RCC_GPIOI_CLK_ENABLE();
    }
    else if (port == GPIOJ)
    {
        __HAL_RCC_GPIOJ_CLK_ENABLE();
    }
    else if (port == GPIOK)
    {
        __HAL_RCC_GPIOK_CLK_ENABLE();
    }
    else
    {
        BSP_ERROR();
    }
}

static void bsp_uart_enable_peripheral_clock(USART_TypeDef *instance)
{
    if (instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
    }
    else if (instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    else if (instance == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
    else if (instance == UART4)
    {
        __HAL_RCC_UART4_CLK_ENABLE();
    }
    else if (instance == UART5)
    {
        __HAL_RCC_UART5_CLK_ENABLE();
    }
    else if (instance == USART6)
    {
        __HAL_RCC_USART6_CLK_ENABLE();
    }
    else if (instance == UART7)
    {
        __HAL_RCC_UART7_CLK_ENABLE();
    }
    else if (instance == UART8)
    {
        __HAL_RCC_UART8_CLK_ENABLE();
    }
    else
    {
        BSP_ERROR();
    }
}

static void bsp_uart_configure_kernel_clock(USART_TypeDef *instance)
{
    RCC_PeriphCLKInitTypeDef clock_config = {0};

    if ((instance == USART1) || (instance == USART6))
    {
        clock_config.PeriphClockSelection = RCC_PERIPHCLK_USART16;
        clock_config.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    }
    else
    {
        clock_config.PeriphClockSelection = RCC_PERIPHCLK_USART234578;
        clock_config.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    }

    if (HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK)
    {
        BSP_ERROR();
    }
}

static void bsp_uart_irq_handler(bsp_uart_device_t *device)
{
    uint32_t interrupt_status = READ_REG(device->instance->ISR);
    uint32_t control = READ_REG(device->instance->CR1);

    if (((interrupt_status & USART_ISR_RXNE_RXFNE) != 0U) &&
        ((control & USART_CR1_RXNEIE_RXFNEIE) != 0U))
    {
        uint8_t received_byte = (uint8_t)READ_REG(device->instance->RDR);
        bsp_uart_rx_callback_t callback = device->rx_callback;

        if (callback != NULL)
        {
            callback(&received_byte, 1U, device->rx_argument);
        }
    }

    if (((interrupt_status & USART_ISR_TXE_TXFNF) != 0U) &&
        ((control & USART_CR1_TXEIE) != 0U))
    {
        if (device->tx_count > 0U)
        {
            WRITE_REG(device->instance->TDR, device->tx_buffer[device->tx_read]);
            device->tx_read++;
            if (device->tx_read >= device->tx_buffer_size)
            {
                device->tx_read = 0U;
            }
            device->tx_count--;
        }
        else
        {
            __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TXE);
            __HAL_UART_ENABLE_IT(&device->handle, UART_IT_TC);
        }
    }

    if (((interrupt_status & USART_ISR_TC) != 0U) &&
        ((control & USART_CR1_TCIE) != 0U))
    {
        WRITE_REG(device->instance->ICR, USART_ICR_TCCF);

        if (device->tx_count == 0U)
        {
            bsp_uart_tx_callback_t callback = device->send_complete;

            __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TC);
            device->tx_state = BSP_UART_TX_IDLE;

            if (callback != NULL)
            {
                callback(device->tx_argument);
            }
        }
        else
        {
            __HAL_UART_DISABLE_IT(&device->handle, UART_IT_TC);
            __HAL_UART_ENABLE_IT(&device->handle, UART_IT_TXE);
        }
    }

    WRITE_REG(device->instance->ICR,
              USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF);
}

static uint32_t bsp_uart_enter_critical(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    return interrupt_state;
}

static void bsp_uart_exit_critical(uint32_t interrupt_state)
{
    __DMB();
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}

#if BSP_UART1_ENABLED
void USART1_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart1_device);
}
#endif

#if BSP_UART2_ENABLED
void USART2_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart2_device);
}
#endif

#if BSP_UART3_ENABLED
void USART3_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart3_device);
}
#endif

#if BSP_UART4_ENABLED
void UART4_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart4_device);
}
#endif

#if BSP_UART5_ENABLED
void UART5_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart5_device);
}
#endif

#if BSP_UART6_ENABLED
void USART6_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart6_device);
}
#endif

#if BSP_UART7_ENABLED
void UART7_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart7_device);
}
#endif

#if BSP_UART8_ENABLED
void UART8_IRQHandler(void)
{
    bsp_uart_irq_handler(&uart8_device);
}
#endif
