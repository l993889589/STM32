/**
 * @file board_uart.c
 * @brief Logical board UART roles and STM32H5 UART binding.
 */

#include "bsp_uart.h"

#include "bsp_uart_stm32h5.h"
#include "stm32h5xx_hal.h"

static bsp_uart_stm32h5_context_t board_uart_contexts[BOARD_UART_COUNT];

/**
 * @brief Enable clocks and configure GPIO alternate functions for one logical UART.
 */
static bsp_status_t board_uart_hardware_init(board_uart_role_t role,
                                             USART_TypeDef **instance)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    IRQn_Type irq;

    if(instance == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    switch(role)
    {
        case BOARD_UART_DEBUG:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART3;
            peripheral_clock.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
            __HAL_RCC_USART3_CLK_ENABLE();
            __HAL_RCC_GPIOC_CLK_ENABLE();
            gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
            gpio.Alternate = GPIO_AF7_USART3;
            gpio.Pull = GPIO_PULLUP;
            *instance = USART3;
            irq = USART3_IRQn;
            break;
        case BOARD_UART_WIFI:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
            peripheral_clock.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
            __HAL_RCC_USART1_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
            gpio.Alternate = GPIO_AF7_USART1;
            gpio.Pull = GPIO_NOPULL;
            *instance = USART1;
            irq = USART1_IRQn;
            break;
        case BOARD_UART_RS485_1:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART2;
            peripheral_clock.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
            __HAL_RCC_USART2_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
            gpio.Alternate = GPIO_AF7_USART2;
            gpio.Pull = GPIO_NOPULL;
            *instance = USART2;
            irq = USART2_IRQn;
            break;
        case BOARD_UART_RS485_2:
            peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_UART4;
            peripheral_clock.Uart4ClockSelection = RCC_UART4CLKSOURCE_PCLK1;
            __HAL_RCC_UART4_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
            gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
            gpio.Alternate = GPIO_AF8_UART4;
            gpio.Pull = GPIO_NOPULL;
            *instance = UART4;
            irq = UART4_IRQn;
            break;
        default:
            return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(role == BOARD_UART_DEBUG ? GPIOC : GPIOA, &gpio);
    HAL_NVIC_SetPriority(irq, 10U, 0U);
    HAL_NVIC_EnableIRQ(irq);
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_uart_init() as documented by its interface contract.
 */
bsp_status_t bsp_uart_init(board_uart_role_t role, const bsp_uart_config_t *config)
{
    USART_TypeDef *instance = NULL;
    bsp_status_t status;

    if(role >= BOARD_UART_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = board_uart_hardware_init(role, &instance);
    return status == BSP_STATUS_OK ?
           bsp_uart_stm32h5_init(&board_uart_contexts[role], instance, config) : status;
}

/**
 * @brief Implement bsp_uart_try_read() as documented by its interface contract.
 */
bsp_status_t bsp_uart_try_read(board_uart_role_t role,
                               uint8_t *data,
                               uint32_t capacity,
                               uint32_t *length)
{
    return role >= BOARD_UART_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_uart_stm32h5_try_read(&board_uart_contexts[role], data, capacity, length);
}

/**
 * @brief Implement bsp_uart_write() as documented by its interface contract.
 */
bsp_status_t bsp_uart_write(board_uart_role_t role,
                            const uint8_t *data,
                            uint32_t length,
                            uint32_t timeout_ms)
{
    return role >= BOARD_UART_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_uart_stm32h5_write(&board_uart_contexts[role], data, length, timeout_ms);
}

/**
 * @brief Implement bsp_uart_get_diagnostics() as documented by its interface contract.
 */
bsp_status_t bsp_uart_get_diagnostics(board_uart_role_t role,
                                      bsp_uart_diagnostics_t *diagnostics)
{
    if((role >= BOARD_UART_COUNT) || (diagnostics == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *diagnostics = board_uart_contexts[role].diagnostics;
    return board_uart_contexts[role].is_initialized ? BSP_STATUS_OK : BSP_STATUS_NOT_READY;
}

/** @brief Dispatch the USART1 vector to the Wi-Fi UART owner. */
void USART1_IRQHandler(void)
{
    bsp_uart_stm32h5_irq(&board_uart_contexts[BOARD_UART_WIFI]);
}

/** @brief Dispatch the USART2 vector to the first RS-485 UART owner. */
void USART2_IRQHandler(void)
{
    bsp_uart_stm32h5_irq(&board_uart_contexts[BOARD_UART_RS485_1]);
}

/** @brief Dispatch the USART3 vector to the debug UART owner. */
void USART3_IRQHandler(void)
{
    bsp_uart_stm32h5_irq(&board_uart_contexts[BOARD_UART_DEBUG]);
}

/** @brief Dispatch the UART4 vector to the second RS-485 UART owner. */
void UART4_IRQHandler(void)
{
    bsp_uart_stm32h5_irq(&board_uart_contexts[BOARD_UART_RS485_2]);
}
