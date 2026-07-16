/**
 * @file mcu_spi.c
 * @brief STM32H5 SPI bus implementation.
 */

#include "mcu_spi.h"

#include <limits.h>

/**
 * @brief Choose the fastest SPI prescaler that does not exceed the request.
 */
static bool mcu_spi_solve_prescaler(uint32_t kernel_clock_hz,
                                    uint32_t requested_hz,
                                    uint32_t *prescaler,
                                    uint32_t *achieved_baud_rate_hz)
{
    static const uint32_t divisors[] = {2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U};
    static const uint32_t values[] =
    {
        SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4,
        SPI_BAUDRATEPRESCALER_8, SPI_BAUDRATEPRESCALER_16,
        SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
        SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
    };
    uint32_t index;

    for(index = 0U; index < (sizeof(divisors) / sizeof(divisors[0])); index++)
    {
        if((kernel_clock_hz / divisors[index]) <= requested_hz)
        {
            *prescaler = values[index];
            *achieved_baud_rate_hz = kernel_clock_hz / divisors[index];
            return true;
        }
    }
    return false;
}

/** @brief Translate HAL SPI completion status into the shared BSP domain. */
static bsp_status_t mcu_spi_from_hal(HAL_StatusTypeDef status)
{
    if(status == HAL_OK)
    {
        return BSP_STATUS_OK;
    }
    if(status == HAL_TIMEOUT)
    {
        return BSP_STATUS_TIMEOUT;
    }
    if(status == HAL_BUSY)
    {
        return BSP_STATUS_BUSY;
    }
    return BSP_STATUS_IO_ERROR;
}

/**
 * @brief Implement mcu_spi_init() as documented by its interface contract.
 */
bsp_status_t mcu_spi_init(mcu_spi_context_t *context,
                                  SPI_TypeDef *instance,
                                  uint32_t kernel_clock_hz,
                                  const bsp_spi_config_t *config)
{
    uint32_t prescaler;
    uint32_t achieved_baud_rate_hz;

    if((context == NULL) || (instance == NULL) || (config == NULL) ||
       (kernel_clock_hz == 0U) || (config->baud_rate_hz == 0U) ||
       (config->clock_polarity > BSP_SPI_CLOCK_POLARITY_HIGH) ||
       (config->clock_phase > BSP_SPI_CLOCK_PHASE_SECOND_EDGE))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    if(!mcu_spi_solve_prescaler(kernel_clock_hz,
                                config->baud_rate_hz,
                                &prescaler,
                                &achieved_baud_rate_hz))
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    context->handle.Instance = instance;
    context->handle.Init.Mode = SPI_MODE_MASTER;
    context->handle.Init.Direction = SPI_DIRECTION_2LINES;
    context->handle.Init.DataSize = SPI_DATASIZE_8BIT;
    context->handle.Init.CLKPolarity =
        config->clock_polarity == BSP_SPI_CLOCK_POLARITY_HIGH ?
        SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    context->handle.Init.CLKPhase =
        config->clock_phase == BSP_SPI_CLOCK_PHASE_SECOND_EDGE ?
        SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    context->handle.Init.NSS = SPI_NSS_SOFT;
    context->handle.Init.BaudRatePrescaler = prescaler;
    context->handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    context->handle.Init.TIMode = SPI_TIMODE_DISABLE;
    context->handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    context->handle.Init.CRCPolynomial = 0x7U;
    context->handle.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    context->handle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    context->handle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    context->handle.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    context->handle.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    context->handle.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    context->handle.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    context->handle.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    context->handle.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
    context->handle.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;

    if(HAL_SPI_Init(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    context->achieved_baud_rate_hz = achieved_baud_rate_hz;
    context->is_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement mcu_spi_write() as documented by its interface contract.
 */
bsp_status_t mcu_spi_write(mcu_spi_context_t *context,
                                   const uint8_t *data,
                                   uint32_t length,
                                   uint32_t timeout_ms)
{
    if((context == NULL) || (data == NULL) || (length == 0U) ||
       (length > UINT16_MAX) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return mcu_spi_from_hal(HAL_SPI_Transmit(&context->handle,
                                             (uint8_t *)data,
                                             (uint16_t)length,
                                             timeout_ms));
}

/**
 * @brief Implement mcu_spi_read() as documented by its interface contract.
 */
bsp_status_t mcu_spi_read(mcu_spi_context_t *context,
                                  uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms)
{
    if((context == NULL) || (data == NULL) || (length == 0U) ||
       (length > UINT16_MAX) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return mcu_spi_from_hal(HAL_SPI_Receive(&context->handle,
                                            data,
                                            (uint16_t)length,
                                            timeout_ms));
}
