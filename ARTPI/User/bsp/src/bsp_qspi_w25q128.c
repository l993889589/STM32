/**
 * @file bsp_qspi_w25q128.c
 * @brief ART-Pi H750 QSPI W25Q128 BSP implementation.
 */

#include "bsp_qspi_w25q128.h"

#define BSP_QSPI_COMMAND_RESET_ENABLE       0x66U
#define BSP_QSPI_COMMAND_RESET              0x99U
#define BSP_QSPI_COMMAND_READ_ID            0x9FU
#define BSP_QSPI_COMMAND_WRITE_ENABLE       0x06U
#define BSP_QSPI_COMMAND_READ_STATUS_1      0x05U
#define BSP_QSPI_COMMAND_READ_STATUS_2      0x35U
#define BSP_QSPI_COMMAND_WRITE_STATUS_2     0x31U
#define BSP_QSPI_COMMAND_READ               0x03U
#define BSP_QSPI_COMMAND_QUAD_OUTPUT_READ   0x6BU
#define BSP_QSPI_COMMAND_PAGE_PROGRAM       0x02U
#define BSP_QSPI_COMMAND_SECTOR_ERASE       0x20U

#define BSP_QSPI_STATUS_BUSY                0x01U
#define BSP_QSPI_STATUS_2_QUAD_ENABLE       0x02U
#define BSP_QSPI_COMMAND_TIMEOUT_MS         1000U
#define BSP_QSPI_PROGRAM_TIMEOUT_MS         100U
#define BSP_QSPI_ERASE_TIMEOUT_MS           5000U

static QSPI_HandleTypeDef qspi_handle;
static uint8_t qspi_initialized;
static uint8_t qspi_memory_mapped;

static HAL_StatusTypeDef bsp_qspi_w25q128_command(uint8_t instruction,
                                                  uint32_t address_mode,
                                                  uint32_t address,
                                                  uint32_t data_mode,
                                                  uint32_t data_length);
static HAL_StatusTypeDef bsp_qspi_w25q128_write_enable(void);
static HAL_StatusTypeDef bsp_qspi_w25q128_wait_ready(uint32_t timeout_ms);
static HAL_StatusTypeDef bsp_qspi_w25q128_enable_quad(void);
static HAL_StatusTypeDef bsp_qspi_w25q128_program_page(uint32_t address,
                                                       const uint8_t *data,
                                                       size_t length);

/** @brief Perform the bsp_qspi_w25q128_init board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};
    RCC_PeriphCLKInitTypeDef clock_config = {0};
    uint32_t jedec_id;

    if(qspi_initialized != 0U)
    {
        return HAL_OK;
    }

    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    clock_config.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    clock_config.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if(HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK)
    {
        return HAL_ERROR;
    }
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_QSPI_FORCE_RESET();
    __HAL_RCC_QSPI_RELEASE_RESET();

    gpio_config.Pin = GPIO_PIN_6;
    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_config.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOG, &gpio_config);

    gpio_config.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
    gpio_config.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio_config);

    gpio_config.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio_config.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio_config);

    qspi_handle.Instance = QUADSPI;
    qspi_handle.Init.ClockPrescaler = 3U;
    qspi_handle.Init.FifoThreshold = 4U;
    qspi_handle.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    qspi_handle.Init.FlashSize = 22U;
    qspi_handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    qspi_handle.Init.ClockMode = QSPI_CLOCK_MODE_0;
    qspi_handle.Init.FlashID = QSPI_FLASH_ID_1;
    qspi_handle.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    if(HAL_QSPI_Init(&qspi_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if(bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_RESET_ENABLE,
                                 QSPI_ADDRESS_NONE,
                                 0U,
                                 QSPI_DATA_NONE,
                                 0U) != HAL_OK ||
       bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_RESET,
                                 QSPI_ADDRESS_NONE,
                                 0U,
                                 QSPI_DATA_NONE,
                                 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(1U);

    qspi_initialized = 1U;
    if(bsp_qspi_w25q128_read_id(&jedec_id) != HAL_OK ||
       bsp_qspi_w25q128_enable_quad() != HAL_OK)
    {
        qspi_initialized = 0U;
        return HAL_ERROR;
    }
    return HAL_OK;
}

/** @brief Perform the bsp_qspi_w25q128_read_id board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_read_id(uint32_t *jedec_id)
{
    uint8_t id[3];

    if(jedec_id == NULL || qspi_initialized == 0U ||
       qspi_memory_mapped != 0U)
    {
        return HAL_ERROR;
    }
    if(bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_READ_ID,
                                 QSPI_ADDRESS_NONE,
                                 0U,
                                 QSPI_DATA_1_LINE,
                                 sizeof(id)) != HAL_OK ||
       HAL_QSPI_Receive(&qspi_handle, id, BSP_QSPI_COMMAND_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }
    *jedec_id = ((uint32_t)id[0] << 16U) |
                ((uint32_t)id[1] << 8U) |
                (uint32_t)id[2];
    return HAL_OK;
}

/** @brief Perform the bsp_qspi_w25q128_read board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_read(uint32_t address,
                                        uint8_t *data,
                                        size_t length)
{
    if(data == NULL || length == 0U || qspi_initialized == 0U ||
       qspi_memory_mapped != 0U || address >= BSP_QSPI_W25Q128_TOTAL_SIZE ||
       length > (size_t)(BSP_QSPI_W25Q128_TOTAL_SIZE - address))
    {
        return HAL_ERROR;
    }
    if(bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_READ,
                                 QSPI_ADDRESS_1_LINE,
                                 address,
                                 QSPI_DATA_1_LINE,
                                 (uint32_t)length) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_QSPI_Receive(&qspi_handle, data, BSP_QSPI_COMMAND_TIMEOUT_MS);
}

/** @brief Perform the bsp_qspi_w25q128_program board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_program(uint32_t address,
                                           const uint8_t *data,
                                           size_t length)
{
    size_t written = 0U;

    if(data == NULL || length == 0U || qspi_initialized == 0U ||
       qspi_memory_mapped != 0U || address >= BSP_QSPI_W25Q128_TOTAL_SIZE ||
       length > (size_t)(BSP_QSPI_W25Q128_TOTAL_SIZE - address))
    {
        return HAL_ERROR;
    }
    while(written < length)
    {
        uint32_t current = address + (uint32_t)written;
        size_t chunk = BSP_QSPI_W25Q128_PAGE_SIZE -
                       (current & (BSP_QSPI_W25Q128_PAGE_SIZE - 1UL));
        if(chunk > length - written)
        {
            chunk = length - written;
        }
        if(bsp_qspi_w25q128_program_page(current, &data[written], chunk) != HAL_OK)
        {
            return HAL_ERROR;
        }
        written += chunk;
    }
    return HAL_OK;
}

/** @brief Perform the bsp_qspi_w25q128_erase_sector board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_erase_sector(uint32_t address)
{
    if(qspi_initialized == 0U || qspi_memory_mapped != 0U ||
       address >= BSP_QSPI_W25Q128_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }
    address &= ~(BSP_QSPI_W25Q128_SECTOR_SIZE - 1UL);
    if(bsp_qspi_w25q128_write_enable() != HAL_OK ||
       bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_SECTOR_ERASE,
                                 QSPI_ADDRESS_1_LINE,
                                 address,
                                 QSPI_DATA_NONE,
                                 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return bsp_qspi_w25q128_wait_ready(BSP_QSPI_ERASE_TIMEOUT_MS);
}

/** @brief Perform the bsp_qspi_w25q128_enter_memory_mapped board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_enter_memory_mapped(void)
{
    QSPI_CommandTypeDef command = {0};
    QSPI_MemoryMappedTypeDef memory_mapped = {0};

    if(qspi_initialized == 0U)
    {
        return HAL_ERROR;
    }
    if(qspi_memory_mapped != 0U)
    {
        return HAL_OK;
    }
    command.Instruction = BSP_QSPI_COMMAND_QUAD_OUTPUT_READ;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = QSPI_DATA_4_LINES;
    command.DummyCycles = 8U;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    memory_mapped.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    if(HAL_QSPI_MemoryMapped(&qspi_handle, &command, &memory_mapped) != HAL_OK)
    {
        return HAL_ERROR;
    }
    qspi_memory_mapped = 1U;
    __DSB();
    __ISB();
    return HAL_OK;
}

/** @brief Perform the bsp_qspi_w25q128_leave_memory_mapped board-support operation. */
HAL_StatusTypeDef bsp_qspi_w25q128_leave_memory_mapped(void)
{
    if(qspi_memory_mapped == 0U)
    {
        return HAL_OK;
    }
    if(HAL_QSPI_Abort(&qspi_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }
    qspi_memory_mapped = 0U;
    return HAL_OK;
}

/** @brief Perform the bsp_qspi_w25q128_command board-support operation. */
static HAL_StatusTypeDef bsp_qspi_w25q128_command(uint8_t instruction,
                                                  uint32_t address_mode,
                                                  uint32_t address,
                                                  uint32_t data_mode,
                                                  uint32_t data_length)
{
    QSPI_CommandTypeDef command = {0};

    command.Instruction = instruction;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = address_mode;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = data_mode;
    command.NbData = data_length;
    command.DummyCycles = 0U;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return HAL_QSPI_Command(&qspi_handle, &command, BSP_QSPI_COMMAND_TIMEOUT_MS);
}

/** @brief Perform the bsp_qspi_w25q128_write_enable board-support operation. */
static HAL_StatusTypeDef bsp_qspi_w25q128_write_enable(void)
{
    return bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_WRITE_ENABLE,
                                     QSPI_ADDRESS_NONE,
                                     0U,
                                     QSPI_DATA_NONE,
                                     0U);
}

/** @brief Perform the bsp_qspi_w25q128_wait_ready board-support operation. */
static HAL_StatusTypeDef bsp_qspi_w25q128_wait_ready(uint32_t timeout_ms)
{
    QSPI_CommandTypeDef command = {0};
    QSPI_AutoPollingTypeDef polling = {0};

    command.Instruction = BSP_QSPI_COMMAND_READ_STATUS_1;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;
    command.DummyCycles = 0U;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    polling.Match = 0U;
    polling.Mask = BSP_QSPI_STATUS_BUSY;
    polling.MatchMode = QSPI_MATCH_MODE_AND;
    polling.StatusBytesSize = 1U;
    polling.Interval = 0x10U;
    polling.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    return HAL_QSPI_AutoPolling(&qspi_handle, &command, &polling, timeout_ms);
}

/** @brief Perform the bsp_qspi_w25q128_enable_quad board-support operation. */
static HAL_StatusTypeDef bsp_qspi_w25q128_enable_quad(void)
{
    uint8_t status;

    if(bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_READ_STATUS_2,
                                 QSPI_ADDRESS_NONE,
                                 0U,
                                 QSPI_DATA_1_LINE,
                                 1U) != HAL_OK ||
       HAL_QSPI_Receive(&qspi_handle, &status, BSP_QSPI_COMMAND_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if((status & BSP_QSPI_STATUS_2_QUAD_ENABLE) != 0U)
    {
        return HAL_OK;
    }
    status |= BSP_QSPI_STATUS_2_QUAD_ENABLE;
    if(bsp_qspi_w25q128_write_enable() != HAL_OK ||
       bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_WRITE_STATUS_2,
                                 QSPI_ADDRESS_NONE,
                                 0U,
                                 QSPI_DATA_1_LINE,
                                 1U) != HAL_OK ||
       HAL_QSPI_Transmit(&qspi_handle, &status, BSP_QSPI_COMMAND_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return bsp_qspi_w25q128_wait_ready(BSP_QSPI_PROGRAM_TIMEOUT_MS);
}

/** @brief Perform the bsp_qspi_w25q128_program_page board-support operation. */
static HAL_StatusTypeDef bsp_qspi_w25q128_program_page(uint32_t address,
                                                       const uint8_t *data,
                                                       size_t length)
{
    if(length == 0U || length > BSP_QSPI_W25Q128_PAGE_SIZE ||
       ((address & (BSP_QSPI_W25Q128_PAGE_SIZE - 1UL)) + length) >
           BSP_QSPI_W25Q128_PAGE_SIZE)
    {
        return HAL_ERROR;
    }
    if(bsp_qspi_w25q128_write_enable() != HAL_OK ||
       bsp_qspi_w25q128_command(BSP_QSPI_COMMAND_PAGE_PROGRAM,
                                 QSPI_ADDRESS_1_LINE,
                                 address,
                                 QSPI_DATA_1_LINE,
                                 (uint32_t)length) != HAL_OK ||
       HAL_QSPI_Transmit(&qspi_handle,
                         (uint8_t *)data,
                         BSP_QSPI_COMMAND_TIMEOUT_MS) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return bsp_qspi_w25q128_wait_ready(BSP_QSPI_PROGRAM_TIMEOUT_MS);
}
