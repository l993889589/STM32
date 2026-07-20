/**
 * @file bsp_spi.c
 * @brief PA5/PA6/PA7 SPI1 mode-0 ownership for STM32F401CC.
 */

#include "bsp_spi.h"

#include <stdbool.h>
#include <string.h>

#include "stm32f4xx_hal.h"

#define BSP_SPI_SCRATCH_SIZE (64U)

static SPI_HandleTypeDef g_spi;
static bool g_spi_initialized;
static volatile bool g_spi_acquired;

/** @brief Initialize the fixed W25Q64-compatible SPI1 bus. */
bsp_status_t bsp_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if(g_spi_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_spi.Instance = SPI1;
    g_spi.Init.Mode = SPI_MODE_MASTER;
    g_spi.Init.Direction = SPI_DIRECTION_2LINES;
    g_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    g_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    g_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    g_spi.Init.NSS = SPI_NSS_SOFT;
    g_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    g_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    g_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    g_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    g_spi.Init.CRCPolynomial = 7U;
    if(HAL_SPI_Init(&g_spi) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    g_spi_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Atomically claim the single-owner polling bus. */
bsp_status_t bsp_spi_acquire(void)
{
    uint32_t state;

    if(!g_spi_initialized)
        return BSP_STATUS_NOT_READY;
    state = __get_PRIMASK();
    __disable_irq();
    if(g_spi_acquired)
    {
        __set_PRIMASK(state);
        return BSP_STATUS_BUSY;
    }
    g_spi_acquired = true;
    __set_PRIMASK(state);
    return BSP_STATUS_OK;
}

/** @brief Atomically release the polling bus. */
void bsp_spi_release(void)
{
    uint32_t state = __get_PRIMASK();

    __disable_irq();
    g_spi_acquired = false;
    __set_PRIMASK(state);
}

/** @brief Exchange caller-owned buffers without shared global staging arrays. */
bsp_status_t bsp_spi_transfer(const uint8_t *tx_data,
                              uint8_t *rx_data,
                              size_t length,
                              uint32_t timeout_ms)
{
    uint8_t tx_scratch[BSP_SPI_SCRATCH_SIZE];
    uint8_t rx_scratch[BSP_SPI_SCRATCH_SIZE];
    HAL_StatusTypeDef hal_status;

    if(length == 0U || timeout_ms == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_spi_initialized)
        return BSP_STATUS_NOT_READY;
    if(!g_spi_acquired)
        return BSP_STATUS_CONFLICT;
    memset(tx_scratch, 0xFF, sizeof(tx_scratch));

    while(length != 0U)
    {
        size_t chunk = length > BSP_SPI_SCRATCH_SIZE ?
                       BSP_SPI_SCRATCH_SIZE : length;
        uint8_t *tx = tx_data != NULL ?
                      (uint8_t *)(uintptr_t)tx_data : tx_scratch;
        uint8_t *rx = rx_data != NULL ? rx_data : rx_scratch;

        hal_status = HAL_SPI_TransmitReceive(&g_spi,
                                             tx,
                                             rx,
                                             (uint16_t)chunk,
                                             timeout_ms);
        if(hal_status != HAL_OK)
        {
            return (hal_status == HAL_TIMEOUT) ? BSP_STATUS_TIMEOUT :
                   ((hal_status == HAL_BUSY) ? BSP_STATUS_BUSY :
                    BSP_STATUS_IO_ERROR);
        }
        if(tx_data != NULL)
            tx_data += chunk;
        if(rx_data != NULL)
            rx_data += chunk;
        length -= chunk;
    }
    return BSP_STATUS_OK;
}
