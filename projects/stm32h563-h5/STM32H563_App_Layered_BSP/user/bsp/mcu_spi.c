/**
 * @file mcu_spi.c
 * @brief STM32H5 SPI solver, DMA, and centralized HAL callback routing.
 */

#include "mcu_spi.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static mcu_spi_context_t *g_mcu_spi_contexts[BOARD_SPI_COUNT];

/** @brief Choose the fastest legal divider that does not exceed the request. */
static bool mcu_spi_solve_prescaler(uint32_t kernel_clock_hz,
                                    uint32_t requested_hz,
                                    uint32_t *prescaler,
                                    uint32_t *achieved_hz)
{
    static const uint16_t divisors[] = {2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U};
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
            *achieved_hz = kernel_clock_hz / divisors[index];
            return true;
        }
    }
    return false;
}

/** @brief Translate HAL completion status to the shared BSP status domain. */
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

/** @brief Resolve a HAL SPI handle to its statically registered owner. */
static mcu_spi_context_t *mcu_spi_find(SPI_HandleTypeDef *handle)
{
    uint32_t index;

    for(index = 0U; index < BOARD_SPI_COUNT; index++)
    {
        if((g_mcu_spi_contexts[index] != NULL) &&
           (&g_mcu_spi_contexts[index]->handle == handle))
        {
            return g_mcu_spi_contexts[index];
        }
    }
    return NULL;
}

/** @brief Validate common blocking transfer arguments. */
static bsp_status_t mcu_spi_validate(const mcu_spi_context_t *context,
                                     const void *data,
                                     uint32_t length,
                                     uint32_t timeout_ms)
{
    if((context == NULL) || (data == NULL) || (length == 0U) ||
       (length > UINT16_MAX) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    return context->is_initialized ? BSP_STATUS_OK : BSP_STATUS_NOT_READY;
}

/** @brief Implement mcu_spi_init() and register one static owner. */
bsp_status_t mcu_spi_init(mcu_spi_context_t *context,
                          board_spi_role_t role,
                          SPI_TypeDef *instance,
                          uint32_t kernel_clock_hz,
                          const bsp_spi_config_t *config)
{
    uint32_t prescaler;
    uint32_t achieved_hz;

    if((context == NULL) || (role >= BOARD_SPI_COUNT) || (instance == NULL) ||
       (config == NULL) || (kernel_clock_hz == 0U) ||
       (config->baud_rate_hz == 0U) ||
       (config->clock_polarity > BSP_SPI_CLOCK_POLARITY_HIGH) ||
       (config->clock_phase > BSP_SPI_CLOCK_PHASE_SECOND_EDGE))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return context->handle.Instance == instance ?
               BSP_STATUS_ALREADY_INITIALIZED : BSP_STATUS_CONFLICT;
    }
    if(!mcu_spi_solve_prescaler(kernel_clock_hz,
                                config->baud_rate_hz,
                                &prescaler,
                                &achieved_hz))
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    memset(context, 0, sizeof(*context));
    context->role = role;
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

    context->achieved_baud_rate_hz = achieved_hz;
    context->is_initialized = true;
    g_mcu_spi_contexts[role] = context;
    return BSP_STATUS_OK;
}

/** @brief Implement context-owned GPDMA TX setup. */
bsp_status_t mcu_spi_configure_tx_dma(mcu_spi_context_t *context,
                                      DMA_Channel_TypeDef *instance,
                                      uint32_t request)
{
    if((context == NULL) || !context->is_initialized || (instance == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    context->tx_dma.Instance = instance;
    context->tx_dma.Init.Request = request;
    context->tx_dma.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    context->tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    context->tx_dma.Init.SrcInc = DMA_SINC_INCREMENTED;
    context->tx_dma.Init.DestInc = DMA_DINC_FIXED;
    context->tx_dma.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
    context->tx_dma.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
    context->tx_dma.Init.Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    context->tx_dma.Init.SrcBurstLength = 1U;
    context->tx_dma.Init.DestBurstLength = 1U;
    context->tx_dma.Init.TransferAllocatedPort =
        DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    context->tx_dma.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    context->tx_dma.Init.Mode = DMA_NORMAL;
    if((HAL_DMA_Init(&context->tx_dma) != HAL_OK) ||
       (HAL_DMA_ConfigChannelAttributes(&context->tx_dma,
                                        DMA_CHANNEL_NPRIV) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_LINKDMA(&context->handle, hdmatx, context->tx_dma);
    context->tx_dma_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Implement bounded blocking transmit. */
bsp_status_t mcu_spi_write(mcu_spi_context_t *context,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    bsp_status_t status = mcu_spi_validate(context, data, length, timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_spi_from_hal(HAL_SPI_Transmit(&context->handle,
                                             (uint8_t *)data,
                                             (uint16_t)length,
                                             timeout_ms)) : status;
}

/** @brief Implement bounded blocking receive. */
bsp_status_t mcu_spi_read(mcu_spi_context_t *context,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    bsp_status_t status = mcu_spi_validate(context, data, length, timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_spi_from_hal(HAL_SPI_Receive(&context->handle,
                                            data,
                                            (uint16_t)length,
                                            timeout_ms)) : status;
}

/** @brief Implement bounded blocking full-duplex exchange. */
bsp_status_t mcu_spi_transfer(mcu_spi_context_t *context,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms)
{
    bsp_status_t status = mcu_spi_validate(context, tx_data, length, timeout_ms);

    if((status != BSP_STATUS_OK) || (rx_data == NULL))
    {
        return rx_data == NULL ? BSP_STATUS_INVALID_ARGUMENT : status;
    }
    return mcu_spi_from_hal(HAL_SPI_TransmitReceive(&context->handle,
                                                    (uint8_t *)tx_data,
                                                    rx_data,
                                                    (uint16_t)length,
                                                    timeout_ms));
}

/** @brief Implement asynchronous context-owned TX DMA. */
bsp_status_t mcu_spi_write_dma(mcu_spi_context_t *context,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument)
{
    if((context == NULL) || !context->is_initialized ||
       !context->tx_dma_initialized || (data == NULL) ||
       (length == 0U) || (length > UINT16_MAX))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    context->tx_callback = callback;
    context->tx_argument = argument;
    return mcu_spi_from_hal(HAL_SPI_Transmit_DMA(&context->handle,
                                                 (uint8_t *)data,
                                                 (uint16_t)length));
}

/** @brief Implement bounded HAL abort for one context. */
bsp_status_t mcu_spi_abort(mcu_spi_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return mcu_spi_from_hal(HAL_SPI_Abort(&context->handle));
}

/** @brief Dispatch a SPI vector to its owned HAL handle. */
void mcu_spi_irq_from_isr(mcu_spi_context_t *context)
{
    if((context != NULL) && context->is_initialized)
    {
        HAL_SPI_IRQHandler(&context->handle);
    }
}

/** @brief Dispatch a TX DMA vector to its owned HAL handle. */
void mcu_spi_tx_dma_irq_from_isr(mcu_spi_context_t *context)
{
    if((context != NULL) && context->tx_dma_initialized)
    {
        HAL_DMA_IRQHandler(&context->tx_dma);
    }
}

/** @brief Route HAL transmit completion to the registered role callback. */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *handle)
{
    mcu_spi_context_t *context = mcu_spi_find(handle);

    if((context != NULL) && (context->tx_callback != NULL))
    {
        context->tx_callback(context->role, BSP_STATUS_OK, context->tx_argument);
    }
}

/** @brief Route HAL SPI errors to the registered role callback. */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle)
{
    mcu_spi_context_t *context = mcu_spi_find(handle);

    if((context != NULL) && (context->tx_callback != NULL))
    {
        context->tx_callback(context->role,
                             BSP_STATUS_IO_ERROR,
                             context->tx_argument);
    }
}
