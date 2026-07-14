#include "bsp.h"

#include <string.h>

#define BSP_SPI_INSTANCE                SPI1
#define BSP_SPI_IRQ_NUMBER              SPI1_IRQn
#define BSP_SPI_IRQ_PRIORITY            6U

#define BSP_SPI_SCK_PORT                GPIOA
#define BSP_SPI_SCK_PIN                 GPIO_PIN_5
#define BSP_SPI_SCK_ALTERNATE           GPIO_AF5_SPI1

#define BSP_SPI_MISO_PORT               GPIOG
#define BSP_SPI_MISO_PIN                GPIO_PIN_9
#define BSP_SPI_MISO_ALTERNATE          GPIO_AF5_SPI1

#define BSP_SPI_MOSI_PORT               GPIOB
#define BSP_SPI_MOSI_PIN                GPIO_PIN_5
#define BSP_SPI_MOSI_ALTERNATE          GPIO_AF5_SPI1

#define BSP_SPI_RX_DMA_STREAM           DMA1_Stream1
#define BSP_SPI_RX_DMA_REQUEST          DMA_REQUEST_SPI1_RX
#define BSP_SPI_RX_DMA_IRQ_NUMBER       DMA1_Stream1_IRQn

#define BSP_SPI_TX_DMA_STREAM           DMA1_Stream2
#define BSP_SPI_TX_DMA_REQUEST          DMA_REQUEST_SPI1_TX
#define BSP_SPI_TX_DMA_IRQ_NUMBER       DMA1_Stream2_IRQn

typedef enum
{
    BSP_SPI_TRANSFER_WAIT = 0,
    BSP_SPI_TRANSFER_COMPLETE,
    BSP_SPI_TRANSFER_ERROR
} bsp_spi_transfer_state_t;

typedef struct
{
    uint8_t tx[BSP_SPI_BUFFER_SIZE];
    uint8_t rx[BSP_SPI_BUFFER_SIZE];
} bsp_spi_dma_buffers_t;

static SPI_HandleTypeDef spi_handle;

#if BSP_SPI_TRANSFER_MODE == BSP_SPI_TRANSFER_MODE_DMA
static DMA_HandleTypeDef spi_rx_dma_handle;
static DMA_HandleTypeDef spi_tx_dma_handle;
#endif

/*
 * DMA1 cannot access DTCM at 0x20000000. The project scatter file places only
 * this section in the non-cacheable AXI SRAM configured by mpu_config(). Keep
 * the same buffer placement for all transfer modes so a macro switch does not
 * alter the project memory layout.
 */
__attribute__((section(".bss.bsp_dma"), aligned(32)))
static bsp_spi_dma_buffers_t spi_dma_buffers;

static volatile bsp_spi_transfer_state_t spi_transfer_state;
static volatile uint8_t spi_bus_locked;
static uint8_t spi_initialized;
static uint32_t spi_baud_rate_prescaler;
static uint32_t spi_clock_phase;
static uint32_t spi_clock_polarity;

static HAL_StatusTypeDef bsp_spi_bus_transfer_chunk(uint16_t length,
                                                    uint32_t timeout_ms);
static uint8_t bsp_spi_timeout_elapsed(uint32_t start_cycles,
                                       uint32_t timeout_ms);
static void bsp_spi_restore_interrupts(uint32_t interrupt_state);

HAL_StatusTypeDef bsp_spi_bus_init(void)
{
    RCC_PeriphCLKInitTypeDef clock_config = {0};

    spi_bus_locked = 0U;
    spi_transfer_state = BSP_SPI_TRANSFER_WAIT;
    spi_baud_rate_prescaler = 0U;
    spi_clock_phase = 0U;
    spi_clock_polarity = 0U;

    clock_config.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
    clock_config.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (bsp_spi_bus_config(SPI_BAUDRATEPRESCALER_2,
                           SPI_PHASE_1EDGE,
                           SPI_POLARITY_LOW) != HAL_OK)
    {
        return HAL_ERROR;
    }

    spi_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_spi_bus_config(uint32_t baud_rate_prescaler,
                                     uint32_t clock_phase,
                                     uint32_t clock_polarity)
{
    if ((spi_handle.State != HAL_SPI_STATE_RESET) &&
        (spi_baud_rate_prescaler == baud_rate_prescaler) &&
        (spi_clock_phase == clock_phase) &&
        (spi_clock_polarity == clock_polarity))
    {
        return HAL_OK;
    }

    if ((spi_handle.State != HAL_SPI_STATE_RESET) &&
        (spi_handle.State != HAL_SPI_STATE_READY))
    {
        return HAL_BUSY;
    }

    spi_handle.Instance = BSP_SPI_INSTANCE;
    spi_handle.Init.Mode = SPI_MODE_MASTER;
    spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
    spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
    spi_handle.Init.CLKPolarity = clock_polarity;
    spi_handle.Init.CLKPhase = clock_phase;
    spi_handle.Init.NSS = SPI_NSS_SOFT;
    spi_handle.Init.BaudRatePrescaler = baud_rate_prescaler;
    spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi_handle.Init.TIMode = SPI_TIMODE_DISABLE;
    spi_handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi_handle.Init.CRCPolynomial = 0U;
    spi_handle.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    spi_handle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    spi_handle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_08DATA;
    spi_handle.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    spi_handle.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    spi_handle.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    spi_handle.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    spi_handle.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    spi_handle.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    spi_handle.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&spi_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    spi_baud_rate_prescaler = baud_rate_prescaler;
    spi_clock_phase = clock_phase;
    spi_clock_polarity = clock_polarity;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_spi_bus_enter(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    __DMB();

    if (spi_bus_locked != 0U)
    {
        bsp_spi_restore_interrupts(interrupt_state);
        return HAL_BUSY;
    }

    spi_bus_locked = 1U;
    bsp_spi_restore_interrupts(interrupt_state);
    return HAL_OK;
}

void bsp_spi_bus_exit(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    spi_bus_locked = 0U;
    bsp_spi_restore_interrupts(interrupt_state);
}

uint8_t bsp_spi_bus_busy(void)
{
    return spi_bus_locked;
}

HAL_StatusTypeDef bsp_spi_bus_transfer(const uint8_t *tx_data,
                                       uint8_t *rx_data,
                                       size_t length,
                                       uint32_t timeout_ms)
{
    size_t offset = 0U;

    if ((spi_initialized == 0U) || (length == 0U))
    {
        return HAL_ERROR;
    }

    if (spi_bus_locked == 0U)
    {
        return HAL_BUSY;
    }

    while (offset < length)
    {
        size_t remaining = length - offset;
        uint16_t chunk_length = (remaining > BSP_SPI_BUFFER_SIZE) ?
                                (uint16_t)BSP_SPI_BUFFER_SIZE :
                                (uint16_t)remaining;
        HAL_StatusTypeDef status;

        if (tx_data != NULL)
        {
            memcpy(spi_dma_buffers.tx, &tx_data[offset], chunk_length);
        }
        else
        {
            memset(spi_dma_buffers.tx, BSP_SPI_DUMMY_BYTE, chunk_length);
        }

        status = bsp_spi_bus_transfer_chunk(chunk_length, timeout_ms);
        if (status != HAL_OK)
        {
            return status;
        }

        if (rx_data != NULL)
        {
            memcpy(&rx_data[offset], spi_dma_buffers.rx, chunk_length);
        }

        offset += chunk_length;
    }

    return HAL_OK;
}

uint32_t bsp_spi_bus_get_transfer_mode(void)
{
    return BSP_SPI_TRANSFER_MODE;
}

static HAL_StatusTypeDef bsp_spi_bus_transfer_chunk(uint16_t length,
                                                    uint32_t timeout_ms)
{
#if BSP_SPI_TRANSFER_MODE == BSP_SPI_TRANSFER_MODE_POLLING
    return HAL_SPI_TransmitReceive(&spi_handle,
                                   spi_dma_buffers.tx,
                                   spi_dma_buffers.rx,
                                   length,
                                   timeout_ms);
#else
    HAL_StatusTypeDef status;
    uint32_t start_cycles;

    spi_transfer_state = BSP_SPI_TRANSFER_WAIT;
    __DMB();

#if BSP_SPI_TRANSFER_MODE == BSP_SPI_TRANSFER_MODE_INTERRUPT
    status = HAL_SPI_TransmitReceive_IT(&spi_handle,
                                        spi_dma_buffers.tx,
                                        spi_dma_buffers.rx,
                                        length);
#else
    status = HAL_SPI_TransmitReceive_DMA(&spi_handle,
                                         spi_dma_buffers.tx,
                                         spi_dma_buffers.rx,
                                         length);
#endif

    if (status != HAL_OK)
    {
        return status;
    }

    start_cycles = bsp_dwt_get_cycles();
    while (spi_transfer_state == BSP_SPI_TRANSFER_WAIT)
    {
        if (bsp_spi_timeout_elapsed(start_cycles, timeout_ms) != 0U)
        {
            (void)HAL_SPI_Abort(&spi_handle);
            return HAL_TIMEOUT;
        }
    }

    return (spi_transfer_state == BSP_SPI_TRANSFER_COMPLETE) ? HAL_OK : HAL_ERROR;
#endif
}

static uint8_t bsp_spi_timeout_elapsed(uint32_t start_cycles,
                                       uint32_t timeout_ms)
{
    uint64_t timeout_cycles;

    if (timeout_ms == HAL_MAX_DELAY)
    {
        return 0U;
    }

    timeout_cycles = ((uint64_t)SystemCoreClock * timeout_ms) / 1000U;
    if (timeout_cycles > UINT32_MAX)
    {
        timeout_cycles = UINT32_MAX;
    }

    return (bsp_dwt_elapsed_cycles(start_cycles) >= (uint32_t)timeout_cycles) ? 1U : 0U;
}

static void bsp_spi_restore_interrupts(uint32_t interrupt_state)
{
    __DMB();
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio_config = {0};

    if (handle->Instance != BSP_SPI_INSTANCE)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio_config.Pin = BSP_SPI_SCK_PIN;
    gpio_config.Alternate = BSP_SPI_SCK_ALTERNATE;
    HAL_GPIO_Init(BSP_SPI_SCK_PORT, &gpio_config);

    gpio_config.Pin = BSP_SPI_MISO_PIN;
    gpio_config.Alternate = BSP_SPI_MISO_ALTERNATE;
    HAL_GPIO_Init(BSP_SPI_MISO_PORT, &gpio_config);

    gpio_config.Pin = BSP_SPI_MOSI_PIN;
    gpio_config.Alternate = BSP_SPI_MOSI_ALTERNATE;
    HAL_GPIO_Init(BSP_SPI_MOSI_PORT, &gpio_config);

#if BSP_SPI_TRANSFER_MODE == BSP_SPI_TRANSFER_MODE_DMA
    __HAL_RCC_DMA1_CLK_ENABLE();

    spi_rx_dma_handle.Instance = BSP_SPI_RX_DMA_STREAM;
    spi_rx_dma_handle.Init.Request = BSP_SPI_RX_DMA_REQUEST;
    spi_rx_dma_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;
    spi_rx_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
    spi_rx_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
    spi_rx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    spi_rx_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    spi_rx_dma_handle.Init.Mode = DMA_NORMAL;
    spi_rx_dma_handle.Init.Priority = DMA_PRIORITY_HIGH;
    spi_rx_dma_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&spi_rx_dma_handle) != HAL_OK)
    {
        BSP_ERROR();
    }
    __HAL_LINKDMA(handle, hdmarx, spi_rx_dma_handle);

    spi_tx_dma_handle.Instance = BSP_SPI_TX_DMA_STREAM;
    spi_tx_dma_handle.Init.Request = BSP_SPI_TX_DMA_REQUEST;
    spi_tx_dma_handle.Init.Direction = DMA_MEMORY_TO_PERIPH;
    spi_tx_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
    spi_tx_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
    spi_tx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    spi_tx_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    spi_tx_dma_handle.Init.Mode = DMA_NORMAL;
    spi_tx_dma_handle.Init.Priority = DMA_PRIORITY_LOW;
    spi_tx_dma_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&spi_tx_dma_handle) != HAL_OK)
    {
        BSP_ERROR();
    }
    __HAL_LINKDMA(handle, hdmatx, spi_tx_dma_handle);

    HAL_NVIC_SetPriority(BSP_SPI_RX_DMA_IRQ_NUMBER, BSP_SPI_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BSP_SPI_RX_DMA_IRQ_NUMBER);
    HAL_NVIC_SetPriority(BSP_SPI_TX_DMA_IRQ_NUMBER, BSP_SPI_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BSP_SPI_TX_DMA_IRQ_NUMBER);
#endif

#if BSP_SPI_TRANSFER_MODE != BSP_SPI_TRANSFER_MODE_POLLING
    HAL_NVIC_SetPriority(BSP_SPI_IRQ_NUMBER, BSP_SPI_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BSP_SPI_IRQ_NUMBER);
#endif
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *handle)
{
    if (handle == &spi_handle)
    {
        spi_transfer_state = BSP_SPI_TRANSFER_COMPLETE;
        __DMB();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *handle)
{
    if (handle == &spi_handle)
    {
        spi_transfer_state = BSP_SPI_TRANSFER_ERROR;
        __DMB();
    }
}

#if BSP_SPI_TRANSFER_MODE != BSP_SPI_TRANSFER_MODE_POLLING
void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&spi_handle);
}
#endif

#if BSP_SPI_TRANSFER_MODE == BSP_SPI_TRANSFER_MODE_DMA
void DMA1_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&spi_rx_dma_handle);
}

void DMA1_Stream2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&spi_tx_dma_handle);
}
#endif
