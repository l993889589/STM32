/**
 * @file board_spi.c
 * @brief SPI1 flash and SPI2 display board bindings with owned LCD TX DMA.
 */

#include "board_spi.h"

#include <stddef.h>

#include "board_gpio.h"
#include "board_resources.h"
#include "mcu_spi.h"

static mcu_spi_context_t g_board_spi_contexts[BOARD_SPI_COUNT];

/** @brief Configure one alternate-function SPI signal pin. */
static void board_spi_configure_pin(GPIO_TypeDef *port,
                                    uint32_t pin,
                                    uint32_t alternate,
                                    uint32_t speed)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = speed;
    gpio.Alternate = alternate;
    HAL_GPIO_Init(port, &gpio);
}

/** @brief Configure board clocks, pins, and optional DMA for one SPI role. */
static bsp_status_t board_spi_hardware_init(board_spi_role_t role,
                                            SPI_TypeDef **instance,
                                            uint32_t *kernel_clock_hz)
{
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if((instance == NULL) || (kernel_clock_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(role == BOARD_SPI_FLASH)
    {
        peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
        peripheral_clock.Spi1ClockSelection = RCC_SPI1CLKSOURCE_PLL1Q;
        if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_SPI1_CLK_ENABLE();
        board_spi_configure_pin(BOARD_SPI_FLASH_SCK_PORT,
                                BOARD_SPI_FLASH_SCK_PIN,
                                BOARD_SPI_FLASH_SCK_AF,
                                GPIO_SPEED_FREQ_HIGH);
        board_spi_configure_pin(BOARD_SPI_FLASH_MISO_PORT,
                                BOARD_SPI_FLASH_MISO_PIN,
                                BOARD_SPI_FLASH_MISO_AF,
                                GPIO_SPEED_FREQ_HIGH);
        board_spi_configure_pin(BOARD_SPI_FLASH_MOSI_PORT,
                                BOARD_SPI_FLASH_MOSI_PIN,
                                BOARD_SPI_FLASH_MOSI_AF,
                                GPIO_SPEED_FREQ_HIGH);
        *instance = BOARD_SPI_FLASH_INSTANCE;
        *kernel_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
    }
    else if(role == BOARD_SPI_DISPLAY)
    {
        peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_SPI2;
        peripheral_clock.Spi2ClockSelection = RCC_SPI2CLKSOURCE_PLL1Q;
        if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_GPDMA1_CLK_ENABLE();
        board_spi_configure_pin(BOARD_SPI_LCD_SCK_PORT,
                                BOARD_SPI_LCD_SCK_PIN,
                                BOARD_SPI_LCD_SCK_AF,
                                GPIO_SPEED_FREQ_VERY_HIGH);
        board_spi_configure_pin(BOARD_SPI_LCD_MOSI_PORT,
                                BOARD_SPI_LCD_MOSI_PIN,
                                BOARD_SPI_LCD_MOSI_AF,
                                GPIO_SPEED_FREQ_VERY_HIGH);
        board_spi_configure_pin(BOARD_SPI_LCD_MISO_PORT,
                                BOARD_SPI_LCD_MISO_PIN,
                                BOARD_SPI_LCD_MISO_AF,
                                GPIO_SPEED_FREQ_VERY_HIGH);
        *instance = BOARD_SPI_LCD_INSTANCE;
        *kernel_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI2);
    }
    else
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    return *kernel_clock_hz == 0U ? BSP_STATUS_IO_ERROR : BSP_STATUS_OK;
}

/** @brief Implement bsp_spi_init() for one logical board role. */
bsp_status_t bsp_spi_init(board_spi_role_t role, const bsp_spi_config_t *config)
{
    SPI_TypeDef *instance = NULL;
    uint32_t kernel_clock_hz = 0U;
    bsp_status_t status;

    if(role >= BOARD_SPI_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = board_spi_hardware_init(role, &instance, &kernel_clock_hz);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = mcu_spi_init(&g_board_spi_contexts[role],
                          role,
                          instance,
                          kernel_clock_hz,
                          config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    if(role == BOARD_SPI_DISPLAY)
    {
        status = mcu_spi_configure_tx_dma(&g_board_spi_contexts[role],
                                          GPDMA1_Channel7,
                                          BOARD_SPI_LCD_TX_DMA_REQUEST);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
        HAL_NVIC_SetPriority(BOARD_SPI_LCD_IRQ, BOARD_SPI_LCD_IRQ_PRIORITY, 0U);
        HAL_NVIC_EnableIRQ(BOARD_SPI_LCD_IRQ);
        HAL_NVIC_SetPriority(GPDMA1_Channel7_IRQn, 5U, 0U);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel7_IRQn);
    }
    return BSP_STATUS_OK;
}

/** @brief Implement achieved-clock query for one SPI role. */
bsp_status_t bsp_spi_get_achieved_baud_rate(board_spi_role_t role,
                                            uint32_t *achieved_baud_rate_hz)
{
    if((role >= BOARD_SPI_COUNT) || (achieved_baud_rate_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_board_spi_contexts[role].is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *achieved_baud_rate_hz = g_board_spi_contexts[role].achieved_baud_rate_hz;
    return BSP_STATUS_OK;
}

/** @brief Implement active-low board chip-select ownership. */
bsp_status_t bsp_spi_select(board_spi_role_t role, uint8_t is_selected)
{
    board_gpio_role_t gpio_role;

    if(role == BOARD_SPI_FLASH)
    {
        gpio_role = BOARD_GPIO_FLASH_CS;
    }
    else if(role == BOARD_SPI_DISPLAY)
    {
        gpio_role = BOARD_GPIO_LCD_CS;
    }
    else
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    return board_gpio_write(gpio_role, is_selected != 0U);
}

/** @brief Implement bounded SPI transmit through the role-owned context. */
bsp_status_t bsp_spi_write(board_spi_role_t role,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           mcu_spi_write(&g_board_spi_contexts[role], data, length, timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement bounded SPI receive through the role-owned context. */
bsp_status_t bsp_spi_read(board_spi_role_t role,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           mcu_spi_read(&g_board_spi_contexts[role], data, length, timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement bounded full-duplex exchange through the owned context. */
bsp_status_t bsp_spi_transfer(board_spi_role_t role,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           mcu_spi_transfer(&g_board_spi_contexts[role],
                            tx_data,
                            rx_data,
                            length,
                            timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement asynchronous TX DMA through the owned context. */
bsp_status_t bsp_spi_write_dma(board_spi_role_t role,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument)
{
    return role < BOARD_SPI_COUNT ?
           mcu_spi_write_dma(&g_board_spi_contexts[role],
                             data,
                             length,
                             callback,
                             argument) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement transfer abort through the owned context. */
bsp_status_t bsp_spi_abort(board_spi_role_t role)
{
    return role < BOARD_SPI_COUNT ?
           mcu_spi_abort(&g_board_spi_contexts[role]) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Dispatch one SPI vector by logical role. */
void board_spi_irq_from_isr(board_spi_role_t role)
{
    if(role < BOARD_SPI_COUNT)
    {
        mcu_spi_irq_from_isr(&g_board_spi_contexts[role]);
    }
}

/** @brief Dispatch one SPI TX DMA vector by logical role. */
void board_spi_tx_dma_irq_from_isr(board_spi_role_t role)
{
    if(role < BOARD_SPI_COUNT)
    {
        mcu_spi_tx_dma_irq_from_isr(&g_board_spi_contexts[role]);
    }
}
