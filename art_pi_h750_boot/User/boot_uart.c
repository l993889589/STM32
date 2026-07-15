#include "boot_uart.h"

#include <stddef.h>
#include <string.h>

static UART_HandleTypeDef boot_uart_handle;

HAL_StatusTypeDef boot_uart_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    gpio_config.Pin = GPIO_PIN_0;
    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_config.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gpio_config);

    gpio_config.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOI, &gpio_config);

    boot_uart_handle.Instance = UART4;
    boot_uart_handle.Init.BaudRate = 115200U;
    boot_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    boot_uart_handle.Init.StopBits = UART_STOPBITS_1;
    boot_uart_handle.Init.Parity = UART_PARITY_NONE;
    boot_uart_handle.Init.Mode = UART_MODE_TX_RX;
    boot_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    boot_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    boot_uart_handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    boot_uart_handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    return HAL_UART_Init(&boot_uart_handle);
}

void boot_uart_write(const char *text)
{
    if(text == NULL)
    {
        return;
    }
    (void)HAL_UART_Transmit(&boot_uart_handle,
                            (uint8_t *)text,
                            (uint16_t)strlen(text),
                            1000U);
}
