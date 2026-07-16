/**
 * @file bsp_spi.c
 * @brief SPI initialization, STM32 hardware control, interrupts, and public BSP API.
 */


#include <stdbool.h>

#include "bsp_spi.h"
#include "stm32h5xx_hal.h"

/** @brief Static mutable state owned by one logical SPI role. */
typedef struct
{
    SPI_HandleTypeDef handle;
    DMA_HandleTypeDef tx_dma;
    bsp_spi_role_t role;
    uint32_t achieved_baud_rate_hz;
    bool is_initialized;
    bool tx_dma_initialized;
    bsp_spi_tx_cb_t tx_callback;
    void *tx_argument;
} bsp_spi_hw_context_t;

/** @brief Initialize one STM32H5 SPI instance. */
bsp_status_t bsp_spi_hw_init(bsp_spi_hw_context_t *context,
                          bsp_spi_role_t role,
                          SPI_TypeDef *instance,
                          uint32_t kernel_clock_hz,
                          const bsp_spi_config_t *config);
/** @brief Configure and link one context-owned GPDMA TX channel. */
bsp_status_t bsp_spi_hw_configure_tx_dma(bsp_spi_hw_context_t *context,
                                      DMA_Channel_TypeDef *instance,
                                      uint32_t request);
/** @brief Execute bounded blocking SPI transmit. */
bsp_status_t bsp_spi_hw_write(bsp_spi_hw_context_t *context,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/** @brief Execute bounded blocking SPI receive. */
bsp_status_t bsp_spi_hw_read(bsp_spi_hw_context_t *context,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);
/** @brief Execute bounded blocking full-duplex SPI exchange. */
bsp_status_t bsp_spi_hw_transfer(bsp_spi_hw_context_t *context,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms);
/** @brief Start asynchronous context-owned TX DMA. */
bsp_status_t bsp_spi_hw_write_dma(bsp_spi_hw_context_t *context,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument);
/** @brief Abort one initialized SPI context. */
bsp_status_t bsp_spi_hw_abort(bsp_spi_hw_context_t *context);
/** @brief Dispatch one SPI vector from ISR context. */
void bsp_spi_hw_irq_from_isr(bsp_spi_hw_context_t *context);
/** @brief Dispatch one SPI TX DMA vector from ISR context. */
void bsp_spi_hw_tx_dma_irq_from_isr(bsp_spi_hw_context_t *context);


#include "bsp_spi.h"

#include <stddef.h>

#include "bsp_gpio.h"
#include "bsp_config.h"
#include "bsp_cache.h"

static bsp_spi_hw_context_t g_bsp_spi_contexts[BOARD_SPI_COUNT];

/** @brief Configure one alternate-function SPI signal pin. */
static void bsp_spi_configure_pin(GPIO_TypeDef *port,
                                    uint32_t pin,
                                    uint32_t alternate,
                                    uint32_t speed)
{
    bsp_status_t status;

    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = speed;
    gpio.Alternate = alternate;
    HAL_GPIO_Init(port, &gpio);
}

/** @brief Configure board clocks, pins, and optional DMA for one SPI role. */
static bsp_status_t bsp_spi_hardware_init(bsp_spi_role_t role,
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
        bsp_spi_configure_pin(BOARD_SPI_FLASH_SCK_PORT,
                                BOARD_SPI_FLASH_SCK_PIN,
                                BOARD_SPI_FLASH_SCK_AF,
                                GPIO_SPEED_FREQ_HIGH);
        bsp_spi_configure_pin(BOARD_SPI_FLASH_MISO_PORT,
                                BOARD_SPI_FLASH_MISO_PIN,
                                BOARD_SPI_FLASH_MISO_AF,
                                GPIO_SPEED_FREQ_HIGH);
        bsp_spi_configure_pin(BOARD_SPI_FLASH_MOSI_PORT,
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
        bsp_spi_configure_pin(BOARD_SPI_LCD_SCK_PORT,
                                BOARD_SPI_LCD_SCK_PIN,
                                BOARD_SPI_LCD_SCK_AF,
                                GPIO_SPEED_FREQ_VERY_HIGH);
        bsp_spi_configure_pin(BOARD_SPI_LCD_MOSI_PORT,
                                BOARD_SPI_LCD_MOSI_PIN,
                                BOARD_SPI_LCD_MOSI_AF,
                                GPIO_SPEED_FREQ_VERY_HIGH);
        bsp_spi_configure_pin(BOARD_SPI_LCD_MISO_PORT,
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
bsp_status_t bsp_spi_init(bsp_spi_role_t role, const bsp_spi_config_t *config)
{
    SPI_TypeDef *instance = NULL;
    uint32_t kernel_clock_hz = 0U;
    bsp_status_t status;

    if(role >= BOARD_SPI_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_spi_hardware_init(role, &instance, &kernel_clock_hz);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = bsp_spi_hw_init(&g_bsp_spi_contexts[role],
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
        status = bsp_spi_hw_configure_tx_dma(&g_bsp_spi_contexts[role],
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
bsp_status_t bsp_spi_get_achieved_baud_rate(bsp_spi_role_t role,
                                            uint32_t *achieved_baud_rate_hz)
{
    if((role >= BOARD_SPI_COUNT) || (achieved_baud_rate_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_bsp_spi_contexts[role].is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *achieved_baud_rate_hz = g_bsp_spi_contexts[role].achieved_baud_rate_hz;
    return BSP_STATUS_OK;
}

/** @brief Implement active-low board chip-select ownership. */
bsp_status_t bsp_spi_select(bsp_spi_role_t role, uint8_t is_selected)
{
    bsp_gpio_role_t gpio_role;

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
    return bsp_gpio_write(gpio_role, is_selected != 0U);
}

/** @brief Implement bounded SPI transmit through the role-owned context. */
bsp_status_t bsp_spi_write(bsp_spi_role_t role,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_hw_write(&g_bsp_spi_contexts[role], data, length, timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement bounded SPI receive through the role-owned context. */
bsp_status_t bsp_spi_read(bsp_spi_role_t role,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_hw_read(&g_bsp_spi_contexts[role], data, length, timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement bounded full-duplex exchange through the owned context. */
bsp_status_t bsp_spi_transfer(bsp_spi_role_t role,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_hw_transfer(&g_bsp_spi_contexts[role],
                            tx_data,
                            rx_data,
                            length,
                            timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement asynchronous TX DMA through the owned context. */
bsp_status_t bsp_spi_write_dma(bsp_spi_role_t role,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_hw_write_dma(&g_bsp_spi_contexts[role],
                             data,
                             length,
                             callback,
                             argument) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement transfer abort through the owned context. */
bsp_status_t bsp_spi_abort(bsp_spi_role_t role)
{
    return role < BOARD_SPI_COUNT ?
           bsp_spi_hw_abort(&g_bsp_spi_contexts[role]) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Dispatch one SPI vector by logical role. */
void bsp_spi_irq_from_isr(bsp_spi_role_t role)
{
    if(role < BOARD_SPI_COUNT)
    {
        bsp_spi_hw_irq_from_isr(&g_bsp_spi_contexts[role]);
    }
}

/** @brief Dispatch one SPI TX DMA vector by logical role. */
void bsp_spi_tx_dma_irq_from_isr(bsp_spi_role_t role)
{
    if(role < BOARD_SPI_COUNT)
    {
        bsp_spi_hw_tx_dma_irq_from_isr(&g_bsp_spi_contexts[role]);
    }
}

/* STM32 hardware implementation. */

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bsp_spi_hw_context_t *g_bsp_spi_hw_contexts[BOARD_SPI_COUNT];

/** @brief Choose the fastest legal divider that does not exceed the request. */
static bool bsp_spi_hw_solve_prescaler(uint32_t kernel_clock_hz,
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
static bsp_status_t bsp_spi_hw_from_hal(HAL_StatusTypeDef status)
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
static bsp_spi_hw_context_t *bsp_spi_hw_find(SPI_HandleTypeDef *handle)
{
    uint32_t index;

    for(index = 0U; index < BOARD_SPI_COUNT; index++)
    {
        if((g_bsp_spi_hw_contexts[index] != NULL) &&
           (&g_bsp_spi_hw_contexts[index]->handle == handle))
        {
            return g_bsp_spi_hw_contexts[index];
        }
    }
    return NULL;
}

/** @brief Validate common blocking transfer arguments. */
static bsp_status_t bsp_spi_hw_validate(const bsp_spi_hw_context_t *context,
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

/** @brief Implement bsp_spi_hw_init() and register one static owner. */
bsp_status_t bsp_spi_hw_init(bsp_spi_hw_context_t *context,
                          bsp_spi_role_t role,
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
    if(!bsp_spi_hw_solve_prescaler(kernel_clock_hz,
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
    g_bsp_spi_hw_contexts[role] = context;
    return BSP_STATUS_OK;
}

/** @brief Implement context-owned GPDMA TX setup. */
bsp_status_t bsp_spi_hw_configure_tx_dma(bsp_spi_hw_context_t *context,
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
bsp_status_t bsp_spi_hw_write(bsp_spi_hw_context_t *context,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    bsp_status_t status = bsp_spi_hw_validate(context, data, length, timeout_ms);
    return status == BSP_STATUS_OK ?
           bsp_spi_hw_from_hal(HAL_SPI_Transmit(&context->handle,
                                             (uint8_t *)data,
                                             (uint16_t)length,
                                             timeout_ms)) : status;
}

/** @brief Implement bounded blocking receive. */
bsp_status_t bsp_spi_hw_read(bsp_spi_hw_context_t *context,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    bsp_status_t status = bsp_spi_hw_validate(context, data, length, timeout_ms);
    return status == BSP_STATUS_OK ?
           bsp_spi_hw_from_hal(HAL_SPI_Receive(&context->handle,
                                            data,
                                            (uint16_t)length,
                                            timeout_ms)) : status;
}

/** @brief Implement bounded blocking full-duplex exchange. */
bsp_status_t bsp_spi_hw_transfer(bsp_spi_hw_context_t *context,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms)
{
    bsp_status_t status = bsp_spi_hw_validate(context, tx_data, length, timeout_ms);

    if((status != BSP_STATUS_OK) || (rx_data == NULL))
    {
        return rx_data == NULL ? BSP_STATUS_INVALID_ARGUMENT : status;
    }
    return bsp_spi_hw_from_hal(HAL_SPI_TransmitReceive(&context->handle,
                                                    (uint8_t *)tx_data,
                                                    rx_data,
                                                    (uint16_t)length,
                                                    timeout_ms));
}

/** @brief Implement asynchronous context-owned TX DMA. */
bsp_status_t bsp_spi_hw_write_dma(bsp_spi_hw_context_t *context,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument)
{
    bsp_status_t status;

    if((context == NULL) || !context->is_initialized ||
       !context->tx_dma_initialized || (data == NULL) ||
       (length == 0U) || (length > UINT16_MAX))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    context->tx_callback = callback;
    context->tx_argument = argument;
    if(bsp_cache_clean(data, length) != BSP_STATUS_OK)
    {
        context->tx_callback = NULL;
        context->tx_argument = NULL;
        return BSP_STATUS_IO_ERROR;
    }
    status = bsp_spi_hw_from_hal(HAL_SPI_Transmit_DMA(&context->handle,
                                                   (uint8_t *)data,
                                                   (uint16_t)length));
    if(status != BSP_STATUS_OK)
    {
        context->tx_callback = NULL;
        context->tx_argument = NULL;
    }
    return status;
}

/** @brief Implement bounded HAL abort for one context. */
bsp_status_t bsp_spi_hw_abort(bsp_spi_hw_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return bsp_spi_hw_from_hal(HAL_SPI_Abort(&context->handle));
}

/** @brief Dispatch a SPI vector to its owned HAL handle. */
void bsp_spi_hw_irq_from_isr(bsp_spi_hw_context_t *context)
{
    if((context != NULL) && context->is_initialized)
    {
        HAL_SPI_IRQHandler(&context->handle);
    }
}

/** @brief Dispatch a TX DMA vector to its owned HAL handle. */
void bsp_spi_hw_tx_dma_irq_from_isr(bsp_spi_hw_context_t *context)
{
    if((context != NULL) && context->tx_dma_initialized)
    {
        HAL_DMA_IRQHandler(&context->tx_dma);
    }
}

/** @brief Route HAL transmit completion to the registered role callback. */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *handle)
{
    bsp_spi_hw_context_t *context = bsp_spi_hw_find(handle);

    if((context != NULL) && (context->tx_callback != NULL))
    {
        bsp_spi_tx_cb_t callback = context->tx_callback;
        void *argument = context->tx_argument;

        context->tx_callback = NULL;
        context->tx_argument = NULL;
        callback(context->role, BSP_STATUS_OK, argument);
    }
}

/** @brief Route HAL SPI errors to the registered role callback. */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle)
{
    bsp_spi_hw_context_t *context = bsp_spi_hw_find(handle);

    if((context != NULL) && (context->tx_callback != NULL))
    {
        bsp_spi_tx_cb_t callback = context->tx_callback;
        void *argument = context->tx_argument;

        context->tx_callback = NULL;
        context->tx_argument = NULL;
        callback(context->role, BSP_STATUS_IO_ERROR, argument);
    }
}
