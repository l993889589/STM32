#include "boot_spi_flash.h"

#include <string.h>

#define BOOT_SPI_FLASH_SIZE              0x01000000UL
#define BOOT_SPI_FLASH_PAGE_SIZE         256UL
#define BOOT_SPI_FLASH_JEDEC_ID          0xEF4018UL

#define BOOT_SPI_COMMAND_WRITE_ENABLE    0x06U
#define BOOT_SPI_COMMAND_READ_STATUS     0x05U
#define BOOT_SPI_COMMAND_READ            0x03U
#define BOOT_SPI_COMMAND_PAGE_PROGRAM    0x02U
#define BOOT_SPI_COMMAND_READ_ID         0x9FU

#define BOOT_SPI_STATUS_BUSY             0x01U
#define BOOT_SPI_TIMEOUT_MS              5000U

static SPI_HandleTypeDef boot_spi_handle;
static uint8_t boot_spi_dummy[256];
static uint8_t boot_spi_discard[256];

static void boot_spi_select(void);
static void boot_spi_deselect(void);
static HAL_StatusTypeDef boot_spi_transfer(const uint8_t *tx_data,
                                            uint8_t *rx_data,
                                            uint16_t length);
static HAL_StatusTypeDef boot_spi_write_enable(void);
static HAL_StatusTypeDef boot_spi_wait_ready(void);
static HAL_StatusTypeDef boot_spi_page_program(uint32_t address,
                                                const uint8_t *data,
                                                uint16_t length);

HAL_StatusTypeDef boot_spi_flash_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};
    uint8_t command[4] =
    {
        BOOT_SPI_COMMAND_READ_ID, 0xFFU, 0xFFU, 0xFFU
    };
    uint8_t response[4] = {0};
    uint32_t jedec_id;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    clock.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
    clock.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if(HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return HAL_ERROR;
    }

    boot_spi_handle.Instance = SPI1;
    boot_spi_handle.Init.Mode = SPI_MODE_MASTER;
    boot_spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
    boot_spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
    boot_spi_handle.Init.CLKPolarity = SPI_POLARITY_LOW;
    boot_spi_handle.Init.CLKPhase = SPI_PHASE_1EDGE;
    boot_spi_handle.Init.NSS = SPI_NSS_SOFT;
    boot_spi_handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    boot_spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    boot_spi_handle.Init.TIMode = SPI_TIMODE_DISABLE;
    boot_spi_handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    boot_spi_handle.Init.CRCPolynomial = 0U;
    boot_spi_handle.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    boot_spi_handle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    /* Polling 8-bit transfers must drain RX one byte at a time.  With a
       deeper threshold the HAL may mix byte and word stores into an
       unaligned destination and the Cortex-M7 traps the word store. */
    boot_spi_handle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    boot_spi_handle.Init.TxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    boot_spi_handle.Init.RxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    boot_spi_handle.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    boot_spi_handle.Init.MasterInterDataIdleness =
        SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    boot_spi_handle.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    boot_spi_handle.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    boot_spi_handle.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    if(HAL_SPI_Init(&boot_spi_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }
    memset(boot_spi_dummy, 0xFF, sizeof(boot_spi_dummy));

    boot_spi_select();
    if(boot_spi_transfer(command, response, sizeof(command)) != HAL_OK)
    {
        boot_spi_deselect();
        return HAL_ERROR;
    }
    boot_spi_deselect();
    jedec_id = ((uint32_t)response[1] << 16U) |
               ((uint32_t)response[2] << 8U) |
               response[3];
    return (jedec_id == BOOT_SPI_FLASH_JEDEC_ID) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef boot_spi_flash_read(uint32_t address,
                                      uint8_t *data,
                                      size_t length)
{
    uint8_t command[4];
    HAL_StatusTypeDef status;

    if(length == 0U)
    {
        return HAL_OK;
    }
    if(data == NULL || address >= BOOT_SPI_FLASH_SIZE ||
       length > (size_t)(BOOT_SPI_FLASH_SIZE - address) ||
       length > 0xFFFFU)
    {
        return HAL_ERROR;
    }
    command[0] = BOOT_SPI_COMMAND_READ;
    command[1] = (uint8_t)(address >> 16U);
    command[2] = (uint8_t)(address >> 8U);
    command[3] = (uint8_t)address;

    boot_spi_select();
    status = boot_spi_transfer(command, NULL, sizeof(command));
    if(status == HAL_OK)
    {
        status = boot_spi_transfer(NULL, data, (uint16_t)length);
    }
    boot_spi_deselect();
    return status;
}

HAL_StatusTypeDef boot_spi_flash_program(uint32_t address,
                                         const uint8_t *data,
                                         size_t length)
{
    size_t offset = 0U;

    if(length == 0U)
    {
        return HAL_OK;
    }
    if(data == NULL || address >= BOOT_SPI_FLASH_SIZE ||
       length > (size_t)(BOOT_SPI_FLASH_SIZE - address))
    {
        return HAL_ERROR;
    }
    while(offset < length)
    {
        uint32_t current = address + (uint32_t)offset;
        uint32_t page_offset = current & (BOOT_SPI_FLASH_PAGE_SIZE - 1U);
        size_t part = length - offset;

        if(part > (size_t)(BOOT_SPI_FLASH_PAGE_SIZE - page_offset))
        {
            part = BOOT_SPI_FLASH_PAGE_SIZE - page_offset;
        }
        if(boot_spi_page_program(current,
                                 &data[offset],
                                 (uint16_t)part) != HAL_OK)
        {
            return HAL_ERROR;
        }
        offset += part;
    }
    return HAL_OK;
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if(handle->Instance != SPI1)
    {
        return;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;

    gpio.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static void boot_spi_select(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

static void boot_spi_deselect(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static HAL_StatusTypeDef boot_spi_transfer(const uint8_t *tx_data,
                                            uint8_t *rx_data,
                                            uint16_t length)
{
    uint16_t offset = 0U;

    while(offset < length)
    {
        uint16_t part = length - offset;
        uint8_t *tx = (tx_data != NULL) ?
                      (uint8_t *)&tx_data[offset] : boot_spi_dummy;
        uint8_t *rx = (rx_data != NULL) ?
                      &rx_data[offset] : boot_spi_discard;
        HAL_StatusTypeDef status;

        if(part > sizeof(boot_spi_dummy))
        {
            part = sizeof(boot_spi_dummy);
        }
        status = HAL_SPI_TransmitReceive(&boot_spi_handle,
                                         tx,
                                         rx,
                                         part,
                                         100U);
        if(status != HAL_OK)
        {
            return status;
        }
        offset += part;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef boot_spi_write_enable(void)
{
    uint8_t command = BOOT_SPI_COMMAND_WRITE_ENABLE;
    HAL_StatusTypeDef status;

    boot_spi_select();
    status = boot_spi_transfer(&command, NULL, sizeof(command));
    boot_spi_deselect();
    return status;
}

static HAL_StatusTypeDef boot_spi_wait_ready(void)
{
    uint32_t start = HAL_GetTick();

    while((HAL_GetTick() - start) < BOOT_SPI_TIMEOUT_MS)
    {
        uint8_t command[2] = {BOOT_SPI_COMMAND_READ_STATUS, 0xFFU};
        uint8_t response[2] = {0};
        HAL_StatusTypeDef status;

        boot_spi_select();
        status = boot_spi_transfer(command, response, sizeof(command));
        boot_spi_deselect();
        if(status != HAL_OK)
        {
            return status;
        }
        if((response[1] & BOOT_SPI_STATUS_BUSY) == 0U)
        {
            return HAL_OK;
        }
    }
    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef boot_spi_page_program(uint32_t address,
                                                const uint8_t *data,
                                                uint16_t length)
{
    uint8_t command[4];
    HAL_StatusTypeDef status;

    if(boot_spi_write_enable() != HAL_OK)
    {
        return HAL_ERROR;
    }
    command[0] = BOOT_SPI_COMMAND_PAGE_PROGRAM;
    command[1] = (uint8_t)(address >> 16U);
    command[2] = (uint8_t)(address >> 8U);
    command[3] = (uint8_t)address;

    boot_spi_select();
    status = boot_spi_transfer(command, NULL, sizeof(command));
    if(status == HAL_OK)
    {
        status = boot_spi_transfer(data, NULL, length);
    }
    boot_spi_deselect();
    if(status != HAL_OK)
    {
        return status;
    }
    return boot_spi_wait_ready();
}
