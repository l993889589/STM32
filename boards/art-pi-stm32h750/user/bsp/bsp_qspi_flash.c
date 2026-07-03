#include "bsp_qspi_flash.h"

#include <string.h>

#include "quadspi.h"

#define QSPI_CMD_RESET_ENABLE       0x66U
#define QSPI_CMD_RESET_MEMORY       0x99U
#define QSPI_CMD_EXIT_QPI           0xFFU
#define QSPI_CMD_READ_JEDEC_ID      0x9FU
#define QSPI_CMD_READ_STATUS        0x05U
#define QSPI_CMD_WRITE_ENABLE       0x06U
#define QSPI_CMD_READ_DATA          0x03U
#define QSPI_CMD_PAGE_PROGRAM       0x02U
#define QSPI_CMD_SECTOR_ERASE       0x20U
#define QSPI_TIMEOUT_MS             100U
#define QSPI_BUSY_TIMEOUT_MS        5000U
#define QSPI_FLASH_SIZE_BYTES       (8UL * 1024UL * 1024UL)
#define QSPI_SECTOR_SIZE_BYTES      4096UL
#define QSPI_PAGE_SIZE_BYTES        256UL
#define QSPI_READ_MODE_HAL_1_LINE   1U
#define QSPI_READ_MODE_HAL_4_LINE   4U
#define QSPI_READ_MODE_GPIO_1_LINE  11U

static void qspi_gpio_delay(void)
{
    volatile uint32_t i;

    for(i = 0U; i < 20U; i++)
    {
        __NOP();
    }
}

static void qspi_gpio_cs(uint8_t high)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    qspi_gpio_delay();
}

static uint8_t qspi_gpio_transfer(uint8_t data)
{
    uint8_t rx = 0U;
    uint8_t bit;

    for(bit = 0U; bit < 8U; bit++)
    {
        HAL_GPIO_WritePin(GPIOF, GPIO_PIN_8, (data & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        qspi_gpio_delay();
        HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
        qspi_gpio_delay();
        rx <<= 1;
        if(HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_9) == GPIO_PIN_SET)
            rx |= 1U;
        HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
        qspi_gpio_delay();
        data <<= 1;
    }

    return rx;
}

static void qspi_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOF, &gpio);

    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_SET);
}

static int qspi_gpio_read_id(uint8_t raw_id[3])
{
    (void)HAL_QSPI_DeInit(&hqspi);
    qspi_gpio_init();

    qspi_gpio_cs(0U);
    (void)qspi_gpio_transfer(QSPI_CMD_EXIT_QPI);
    qspi_gpio_cs(1U);
    HAL_Delay(1U);

    qspi_gpio_cs(0U);
    (void)qspi_gpio_transfer(QSPI_CMD_RESET_ENABLE);
    qspi_gpio_cs(1U);
    qspi_gpio_cs(0U);
    (void)qspi_gpio_transfer(QSPI_CMD_RESET_MEMORY);
    qspi_gpio_cs(1U);
    HAL_Delay(1U);

    qspi_gpio_cs(0U);
    (void)qspi_gpio_transfer(QSPI_CMD_READ_JEDEC_ID);
    raw_id[0] = qspi_gpio_transfer(0xFFU);
    raw_id[1] = qspi_gpio_transfer(0xFFU);
    raw_id[2] = qspi_gpio_transfer(0xFFU);
    qspi_gpio_cs(1U);

    MX_QUADSPI_Init();
    return 0;
}

static int qspi_send_instruction(uint8_t instruction, uint32_t instruction_mode)
{
    QSPI_CommandTypeDef cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = instruction_mode;
    cmd.Instruction = instruction;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_NONE;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    return (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int qspi_read_id(uint32_t instruction_mode, uint32_t data_mode, uint8_t raw_id[3])
{
    QSPI_CommandTypeDef cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = instruction_mode;
    cmd.Instruction = QSPI_CMD_READ_JEDEC_ID;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = data_mode;
    cmd.NbData = 3U;
    cmd.DummyCycles = 0;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if(HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_MS) != HAL_OK)
        return -2;
    if(HAL_QSPI_Receive(&hqspi, raw_id, QSPI_TIMEOUT_MS) != HAL_OK)
        return -3;

    return 0;
}

static int qspi_read_status(uint8_t *status)
{
    QSPI_CommandTypeDef cmd;

    if(status == NULL)
        return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = QSPI_CMD_READ_STATUS;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 1U;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if(HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_MS) != HAL_OK)
        return -2;
    if(HAL_QSPI_Receive(&hqspi, status, QSPI_TIMEOUT_MS) != HAL_OK)
        return -3;

    return 0;
}

static int qspi_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t status = 0U;

    do
    {
        if(qspi_read_status(&status) != 0)
            return -1;
        if((status & 0x01U) == 0U)
            return 0;
    } while((HAL_GetTick() - start) <= timeout_ms);

    return -2;
}

static int qspi_write_enable(void)
{
    if(qspi_send_instruction(QSPI_CMD_WRITE_ENABLE, QSPI_INSTRUCTION_1_LINE) != 0)
        return -1;

    return 0;
}

static int qspi_address_command(uint8_t instruction,
                                uint32_t address,
                                uint32_t data_mode,
                                uint32_t length)
{
    QSPI_CommandTypeDef cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = instruction;
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize = QSPI_ADDRESS_24_BITS;
    cmd.Address = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = data_mode;
    cmd.NbData = length;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    return (HAL_QSPI_Command(&hqspi, &cmd, QSPI_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static uint8_t qspi_range_valid(uint32_t address, uint32_t length)
{
    return length != 0U &&
           address < QSPI_FLASH_SIZE_BYTES &&
           length <= (QSPI_FLASH_SIZE_BYTES - address);
}

static uint8_t qspi_id_is_valid(const uint8_t raw_id[3])
{
    return !((raw_id[0] == 0xFFU && raw_id[1] == 0xFFU && raw_id[2] == 0xFFU) ||
             (raw_id[0] == 0x00U && raw_id[1] == 0x00U && raw_id[2] == 0x00U));
}

static void qspi_fill_id(bsp_qspi_flash_id_t *id, const uint8_t raw_id[3], uint8_t read_mode)
{
    id->manufacturer_id = raw_id[0];
    id->memory_type = raw_id[1];
    id->capacity_id = raw_id[2];
    id->read_mode = read_mode;
    if(raw_id[2] >= 16U && raw_id[2] <= 32U)
        id->capacity_bytes = 1UL << raw_id[2];
}

int bsp_qspi_flash_read_id(bsp_qspi_flash_id_t *id)
{
    uint8_t raw_id[3] = {0U, 0U, 0U};
    int status;

    if(id == NULL)
        return -1;

    memset(id, 0, sizeof(*id));

    (void)HAL_QSPI_Abort(&hqspi);

    (void)qspi_send_instruction(QSPI_CMD_EXIT_QPI, QSPI_INSTRUCTION_4_LINES);
    HAL_Delay(1U);
    (void)qspi_send_instruction(QSPI_CMD_RESET_ENABLE, QSPI_INSTRUCTION_4_LINES);
    (void)qspi_send_instruction(QSPI_CMD_RESET_MEMORY, QSPI_INSTRUCTION_4_LINES);
    HAL_Delay(1U);

    (void)qspi_send_instruction(QSPI_CMD_EXIT_QPI, QSPI_INSTRUCTION_1_LINE);
    HAL_Delay(1U);
    (void)qspi_send_instruction(QSPI_CMD_RESET_ENABLE, QSPI_INSTRUCTION_1_LINE);
    (void)qspi_send_instruction(QSPI_CMD_RESET_MEMORY, QSPI_INSTRUCTION_1_LINE);
    HAL_Delay(1U);

    status = qspi_read_id(QSPI_INSTRUCTION_1_LINE, QSPI_DATA_1_LINE, raw_id);
    qspi_fill_id(id, raw_id, QSPI_READ_MODE_HAL_1_LINE);
    if(status == 0 && qspi_id_is_valid(raw_id))
        return 0;

    memset(raw_id, 0, sizeof(raw_id));
    (void)HAL_QSPI_Abort(&hqspi);
    status = qspi_read_id(QSPI_INSTRUCTION_4_LINES, QSPI_DATA_4_LINES, raw_id);
    qspi_fill_id(id, raw_id, QSPI_READ_MODE_HAL_4_LINE);
    if(status == 0 && qspi_id_is_valid(raw_id))
        return 0;

    memset(raw_id, 0, sizeof(raw_id));
    status = qspi_gpio_read_id(raw_id);
    qspi_fill_id(id, raw_id, QSPI_READ_MODE_GPIO_1_LINE);
    if(status == 0 && qspi_id_is_valid(raw_id))
        return 0;

    return (status == 0) ? -4 : status;
}

int bsp_qspi_flash_read(uint32_t address, uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

    if(data == NULL || !qspi_range_valid(address, length))
        return -1;

    if(qspi_wait_ready(QSPI_BUSY_TIMEOUT_MS) != 0)
        return -2;

    while(offset < length)
    {
        uint32_t chunk = length - offset;
        if(chunk > 4096U)
            chunk = 4096U;

        if(qspi_address_command(QSPI_CMD_READ_DATA,
                                address + offset,
                                QSPI_DATA_1_LINE,
                                chunk) != 0)
            return -3;
        if(HAL_QSPI_Receive(&hqspi, &data[offset], QSPI_TIMEOUT_MS) != HAL_OK)
            return -4;
        offset += chunk;
    }

    return 0;
}

int bsp_qspi_flash_erase(uint32_t address, uint32_t length)
{
    uint32_t current;
    uint32_t end;

    if(!qspi_range_valid(address, length))
        return -1;

    current = address & ~(QSPI_SECTOR_SIZE_BYTES - 1UL);
    end = (address + length + QSPI_SECTOR_SIZE_BYTES - 1UL) & ~(QSPI_SECTOR_SIZE_BYTES - 1UL);
    if(end > QSPI_FLASH_SIZE_BYTES)
        return -1;

    while(current < end)
    {
        if(qspi_wait_ready(QSPI_BUSY_TIMEOUT_MS) != 0)
            return -2;
        if(qspi_write_enable() != 0)
            return -3;
        if(qspi_address_command(QSPI_CMD_SECTOR_ERASE, current, QSPI_DATA_NONE, 0U) != 0)
            return -4;
        if(qspi_wait_ready(QSPI_BUSY_TIMEOUT_MS) != 0)
            return -5;

        current += QSPI_SECTOR_SIZE_BYTES;
    }

    return 0;
}

int bsp_qspi_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0U;

    if(data == NULL || !qspi_range_valid(address, length))
        return -1;

    while(offset < length)
    {
        uint32_t page_room = QSPI_PAGE_SIZE_BYTES - ((address + offset) & (QSPI_PAGE_SIZE_BYTES - 1UL));
        uint32_t chunk = length - offset;

        if(chunk > page_room)
            chunk = page_room;

        if(qspi_wait_ready(QSPI_BUSY_TIMEOUT_MS) != 0)
            return -2;
        if(qspi_write_enable() != 0)
            return -3;
        if(qspi_address_command(QSPI_CMD_PAGE_PROGRAM,
                                address + offset,
                                QSPI_DATA_1_LINE,
                                chunk) != 0)
            return -4;
        if(HAL_QSPI_Transmit(&hqspi, (uint8_t *)&data[offset], QSPI_TIMEOUT_MS) != HAL_OK)
            return -5;
        if(qspi_wait_ready(QSPI_BUSY_TIMEOUT_MS) != 0)
            return -6;

        offset += chunk;
    }

    return 0;
}

uint32_t bsp_qspi_flash_crc32(uint32_t address, uint32_t length)
{
    uint8_t buffer[256];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0U;

    if(!qspi_range_valid(address, length))
        return 0UL;

    while(offset < length)
    {
        uint32_t chunk = length - offset;
        if(chunk > sizeof(buffer))
            chunk = sizeof(buffer);

        if(bsp_qspi_flash_read(address + offset, buffer, chunk) != 0)
            return 0UL;

        for(uint32_t i = 0U; i < chunk; i++)
        {
            crc ^= buffer[i];
            for(uint32_t bit = 0U; bit < 8U; bit++)
                crc = (crc & 1UL) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
        offset += chunk;
    }

    return crc ^ 0xFFFFFFFFUL;
}
