/**
 * @file board_spi.c
 * @brief SPI1 flash and SPI2 display board bindings.
 */

#include "bsp_spi.h"

#include "bsp_clock.h"
#include "bsp_spi_stm32h5.h"
#include "stm32h5xx_hal.h"

static bsp_spi_stm32h5_context_t board_spi_contexts[BOARD_SPI_COUNT];

/**
 * @brief Configure clocks and alternate-function pins for one logical SPI bus.
 */
static bsp_status_t board_spi_hardware_init(board_spi_role_t role,
                                            SPI_TypeDef **instance)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if(instance == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

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
        gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
        gpio.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
        *instance = SPI1;
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
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_SPI2_CLK_ENABLE();

        gpio.Pin = GPIO_PIN_10;
        gpio.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOB, &gpio);
        gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
        HAL_GPIO_Init(GPIOC, &gpio);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
        *instance = SPI2;
    }
    else
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_spi_init() as documented by its interface contract.
 */
bsp_status_t bsp_spi_init(board_spi_role_t role, const bsp_spi_config_t *config)
{
    SPI_TypeDef *instance = NULL;
    uint32_t kernel_clock_hz;
    bsp_status_t status;

    if(role >= BOARD_SPI_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_clock_get_hz(BSP_CLOCK_SYSCLK, &kernel_clock_hz);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = board_spi_hardware_init(role, &instance);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    return bsp_spi_stm32h5_init(&board_spi_contexts[role],
                                instance,
                                kernel_clock_hz,
                                config);
}

/** @brief Implement bsp_spi_get_achieved_baud_rate() for a logical role. */
bsp_status_t bsp_spi_get_achieved_baud_rate(
    board_spi_role_t role,
    uint32_t *achieved_baud_rate_hz)
{
    if((role >= BOARD_SPI_COUNT) || (achieved_baud_rate_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_spi_contexts[role].is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *achieved_baud_rate_hz =
        board_spi_contexts[role].achieved_baud_rate_hz;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_spi_select() as documented by its interface contract.
 */
bsp_status_t bsp_spi_select(board_spi_role_t role, uint8_t is_selected)
{
    GPIO_TypeDef *port;
    uint16_t pin;

    if(role == BOARD_SPI_FLASH)
    {
        port = GPIOA;
        pin = GPIO_PIN_4;
    }
    else if(role == BOARD_SPI_DISPLAY)
    {
        port = GPIOD;
        pin = GPIO_PIN_11;
    }
    else
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    HAL_GPIO_WritePin(port,
                      pin,
                      is_selected != 0U ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_spi_write() as documented by its interface contract.
 */
bsp_status_t bsp_spi_write(board_spi_role_t role,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_stm32h5_write(&board_spi_contexts[role],
                                 data,
                                 length,
                                 timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/**
 * @brief Implement bsp_spi_read() as documented by its interface contract.
 */
bsp_status_t bsp_spi_read(board_spi_role_t role,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_stm32h5_read(&board_spi_contexts[role],
                                data,
                                length,
                                timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}
