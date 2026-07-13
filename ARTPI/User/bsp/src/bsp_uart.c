#include "bsp.h"
#include <string.h>

static UART_HandleTypeDef uart4_handle;

void bsp_uart4_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock_config = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    peripheral_clock_config.PeriphClockSelection = RCC_PERIPHCLK_UART4;
    peripheral_clock_config.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock_config) != HAL_OK)
    {
        BSP_ERROR();
    }

    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_config.Alternate = GPIO_AF8_UART4;

    gpio_config.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &gpio_config);

    gpio_config.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOI, &gpio_config);

    uart4_handle.Instance = UART4;
    uart4_handle.Init.BaudRate = 115200U;
    uart4_handle.Init.WordLength = UART_WORDLENGTH_8B;
    uart4_handle.Init.StopBits = UART_STOPBITS_1;
    uart4_handle.Init.Parity = UART_PARITY_NONE;
    uart4_handle.Init.Mode = UART_MODE_TX_RX;
    uart4_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart4_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    uart4_handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart4_handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    uart4_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&uart4_handle) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_SetTxFifoThreshold(&uart4_handle, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_SetRxFifoThreshold(&uart4_handle, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        BSP_ERROR();
    }

    if (HAL_UARTEx_DisableFifoMode(&uart4_handle) != HAL_OK)
    {
        BSP_ERROR();
    }
}

void bsp_uart4_write(const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    while (length > 0U)
    {
        uint16_t block_length = (length > UINT16_MAX) ? UINT16_MAX : (uint16_t)length;

        if (HAL_UART_Transmit(&uart4_handle, (uint8_t *)data, block_length, HAL_MAX_DELAY) != HAL_OK)
        {
            BSP_ERROR();
        }

        data += block_length;
        length -= block_length;
    }
}

void bsp_uart4_write_string(const char *text)
{
    if (text != NULL)
    {
        bsp_uart4_write((const uint8_t *)text, strlen(text));
    }
}

