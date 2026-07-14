#include "bsp.h"

/* INDUSTRY-IO SP3485: PI4 drives DE and /RE together. */
#define BSP_RS485_DIRECTION_PORT GPIOI
#define BSP_RS485_DIRECTION_PIN  GPIO_PIN_4

static void bsp_rs485_send_before(void *argument);
static void bsp_rs485_send_complete(void *argument);

HAL_StatusTypeDef bsp_rs485_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* Low selects receive mode. Do this before configuring the output. */
    HAL_GPIO_WritePin(BSP_RS485_DIRECTION_PORT,
                      BSP_RS485_DIRECTION_PIN,
                      GPIO_PIN_RESET);

    gpio_config.Pin = BSP_RS485_DIRECTION_PIN;
    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(BSP_RS485_DIRECTION_PORT, &gpio_config);

    return bsp_uart_set_tx_callbacks(BSP_RS485_UART_PORT,
                                     bsp_rs485_send_before,
                                     bsp_rs485_send_complete,
                                     NULL);
}

HAL_StatusTypeDef bsp_rs485_receive_start(bsp_uart_rx_callback_t callback,
                                           void *argument)
{
    return bsp_uart_receive_start(BSP_RS485_UART_PORT, callback, argument);
}

HAL_StatusTypeDef bsp_rs485_receive_stop(void)
{
    return bsp_uart_receive_stop(BSP_RS485_UART_PORT);
}

size_t bsp_rs485_write(const uint8_t *data, size_t length)
{
    return bsp_uart_write(BSP_RS485_UART_PORT, data, length);
}

uint8_t bsp_rs485_tx_empty(void)
{
    return bsp_uart_tx_empty(BSP_RS485_UART_PORT);
}

static void bsp_rs485_send_before(void *argument)
{
    (void)argument;
    HAL_GPIO_WritePin(BSP_RS485_DIRECTION_PORT,
                      BSP_RS485_DIRECTION_PIN,
                      GPIO_PIN_SET);
}

static void bsp_rs485_send_complete(void *argument)
{
    (void)argument;
    HAL_GPIO_WritePin(BSP_RS485_DIRECTION_PORT,
                      BSP_RS485_DIRECTION_PIN,
                      GPIO_PIN_RESET);
}
