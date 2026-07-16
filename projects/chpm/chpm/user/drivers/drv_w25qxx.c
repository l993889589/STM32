#include "drv_w25qxx.h"

#include "bsp_spi.h"
#include "stm32f4xx_hal.h"

#include <string.h>

#define SF_CS_PIN                GPIO_PIN_4
#define SF_IO_TIMEOUT_MS         100U
#define SF_PAGE_TIMEOUT_MS       100U
#define SF_SECTOR_TIMEOUT_MS     2000U
#define SF_CHIP_TIMEOUT_MS       120000U

#define CMD_WRITE_DISABLE        0x04U
#define CMD_READ_STATUS          0x05U
#define CMD_WRITE_ENABLE         0x06U
#define CMD_PAGE_PROGRAM         0x02U
#define CMD_READ                 0x03U
#define CMD_SECTOR_ERASE         0x20U
#define CMD_CHIP_ERASE           0xC7U
#define CMD_READ_ID              0x9FU
#define STATUS_BUSY              0x01U

SFLASH_T g_tSF;

static sf_status_t s_last_status = SF_STATUS_NOT_READY;

static void sf_select(void)
{
    bsp_SpiBusEnter();
    bsp_InitSPIParam(SPI_BAUDRATEPRESCALER_2,
                     SPI_PHASE_1EDGE,
                     SPI_POLARITY_LOW);
    GPIOA->BSRR = ((uint32_t)SF_CS_PIN << 16U);
}

static void sf_deselect(void)
{
    GPIOA->BSRR = SF_CS_PIN;
    bsp_SpiBusExit();
}

static sf_status_t sf_transfer(size_t length)
{
    if(length == 0U || length > SPI_BUFFER_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;
    g_spiLen = (uint32_t)length;
    return bsp_spiTransferTimeout(SF_IO_TIMEOUT_MS) ?
           SF_STATUS_OK : SF_STATUS_IO_ERROR;
}

static sf_status_t sf_simple_command(uint8_t command)
{
    sf_status_t status;

    g_spiTxBuf[0] = command;
    sf_select();
    status = sf_transfer(1U);
    sf_deselect();
    return status;
}

static sf_status_t sf_read_status(uint8_t *status_register)
{
    sf_status_t status;

    if(status_register == NULL)
        return SF_STATUS_INVALID_ARGUMENT;
    g_spiTxBuf[0] = CMD_READ_STATUS;
    g_spiTxBuf[1] = 0xFFU;
    sf_select();
    status = sf_transfer(2U);
    if(status == SF_STATUS_OK)
        *status_register = g_spiRxBuf[1];
    sf_deselect();
    return status;
}

static sf_status_t sf_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t status_register;
    sf_status_t status;

    do
    {
        status = sf_read_status(&status_register);
        if(status != SF_STATUS_OK)
            return status;
        if((status_register & STATUS_BUSY) == 0U)
            return SF_STATUS_OK;
    } while((HAL_GetTick() - start) < timeout_ms);
    return SF_STATUS_TIMEOUT;
}

static sf_status_t sf_write_enable(void)
{
    return sf_simple_command(CMD_WRITE_ENABLE);
}

static sf_status_t sf_program_page(uint32_t address,
                                   const uint8_t *data,
                                   size_t length)
{
    sf_status_t status;

    if(data == NULL || length == 0U || length > W25Q64_PAGE_SIZE ||
       ((address & (W25Q64_PAGE_SIZE - 1U)) + length) > W25Q64_PAGE_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;

    status = sf_write_enable();
    if(status != SF_STATUS_OK)
        return status;
    g_spiTxBuf[0] = CMD_PAGE_PROGRAM;
    g_spiTxBuf[1] = (uint8_t)(address >> 16);
    g_spiTxBuf[2] = (uint8_t)(address >> 8);
    g_spiTxBuf[3] = (uint8_t)address;
    memcpy(&g_spiTxBuf[4], data, length);
    sf_select();
    status = sf_transfer(length + 4U);
    sf_deselect();
    if(status != SF_STATUS_OK)
        return status;
    return sf_wait_ready(SF_PAGE_TIMEOUT_MS);
}

sf_status_t sf_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t id;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = SF_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    sf_deselect();

    id = sf_ReadID();
    memset(&g_tSF, 0, sizeof(g_tSF));
    g_tSF.ChipID = id;
    if(id != W25Q64_JEDEC_ID)
    {
        s_last_status = SF_STATUS_NOT_READY;
        return s_last_status;
    }
    memcpy(g_tSF.ChipName, "W25Q64JV", 9U);
    g_tSF.TotalSize = W25Q64_TOTAL_SIZE;
    g_tSF.SectorSize = (uint16_t)W25Q64_SECTOR_SIZE;
    s_last_status = SF_STATUS_OK;
    return s_last_status;
}

uint32_t sf_ReadID(void)
{
    sf_status_t status;
    uint32_t id = 0U;

    g_spiTxBuf[0] = CMD_READ_ID;
    g_spiTxBuf[1] = 0xFFU;
    g_spiTxBuf[2] = 0xFFU;
    g_spiTxBuf[3] = 0xFFU;
    sf_select();
    status = sf_transfer(4U);
    if(status == SF_STATUS_OK)
        id = ((uint32_t)g_spiRxBuf[1] << 16) |
             ((uint32_t)g_spiRxBuf[2] << 8) |
             g_spiRxBuf[3];
    sf_deselect();
    s_last_status = status;
    return id;
}

sf_status_t sf_read(uint32_t address, void *data, size_t length)
{
    uint8_t *output = (uint8_t *)data;
    sf_status_t status;

    if((data == NULL && length != 0U) || address > W25Q64_TOTAL_SIZE ||
       length > W25Q64_TOTAL_SIZE - address)
        return SF_STATUS_INVALID_ARGUMENT;
    if(length == 0U)
        return SF_STATUS_OK;

    g_spiTxBuf[0] = CMD_READ;
    g_spiTxBuf[1] = (uint8_t)(address >> 16);
    g_spiTxBuf[2] = (uint8_t)(address >> 8);
    g_spiTxBuf[3] = (uint8_t)address;
    sf_select();
    status = sf_transfer(4U);
    while(status == SF_STATUS_OK && length != 0U)
    {
        size_t chunk = length > SPI_BUFFER_SIZE ? SPI_BUFFER_SIZE : length;
        memset(g_spiTxBuf, 0xFF, chunk);
        status = sf_transfer(chunk);
        if(status == SF_STATUS_OK)
        {
            memcpy(output, g_spiRxBuf, chunk);
            output += chunk;
            length -= chunk;
        }
    }
    sf_deselect();
    s_last_status = status;
    return status;
}

sf_status_t sf_erase_sector_checked(uint32_t address)
{
    sf_status_t status;

    if(address >= W25Q64_TOTAL_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;
    address &= ~(W25Q64_SECTOR_SIZE - 1U);
    status = sf_write_enable();
    if(status != SF_STATUS_OK)
        return status;
    g_spiTxBuf[0] = CMD_SECTOR_ERASE;
    g_spiTxBuf[1] = (uint8_t)(address >> 16);
    g_spiTxBuf[2] = (uint8_t)(address >> 8);
    g_spiTxBuf[3] = (uint8_t)address;
    sf_select();
    status = sf_transfer(4U);
    sf_deselect();
    if(status == SF_STATUS_OK)
        status = sf_wait_ready(SF_SECTOR_TIMEOUT_MS);
    s_last_status = status;
    return status;
}

sf_status_t sf_program(uint32_t address, const void *data, size_t length)
{
    const uint8_t *input = (const uint8_t *)data;
    sf_status_t status = SF_STATUS_OK;

    if((data == NULL && length != 0U) || address > W25Q64_TOTAL_SIZE ||
       length > W25Q64_TOTAL_SIZE - address)
        return SF_STATUS_INVALID_ARGUMENT;
    while(length != 0U)
    {
        size_t page_space = W25Q64_PAGE_SIZE - (address & (W25Q64_PAGE_SIZE - 1U));
        size_t chunk = length < page_space ? length : page_space;
        status = sf_program_page(address, input, chunk);
        if(status != SF_STATUS_OK)
            break;
        address += (uint32_t)chunk;
        input += chunk;
        length -= chunk;
    }
    s_last_status = status;
    return status;
}

sf_status_t sf_verify(uint32_t address, const void *data, size_t length)
{
    const uint8_t *expected = (const uint8_t *)data;
    uint8_t verify[64];
    sf_status_t status;

    if((data == NULL && length != 0U) || address > W25Q64_TOTAL_SIZE ||
       length > W25Q64_TOTAL_SIZE - address)
        return SF_STATUS_INVALID_ARGUMENT;
    while(length != 0U)
    {
        size_t chunk = length < sizeof(verify) ? length : sizeof(verify);
        status = sf_read(address, verify, chunk);
        if(status != SF_STATUS_OK)
            return status;
        if(memcmp(expected, verify, chunk) != 0)
            return SF_STATUS_VERIFY_FAILED;
        address += (uint32_t)chunk;
        expected += chunk;
        length -= chunk;
    }
    return SF_STATUS_OK;
}

sf_status_t sf_last_status(void)
{
    return s_last_status;
}

void bsp_InitSFlash(void)
{
    (void)sf_init();
}

void sf_EraseSector(uint32_t address)
{
    (void)sf_erase_sector_checked(address);
}

void sf_EraseChip(void)
{
    sf_status_t status = sf_write_enable();
    if(status == SF_STATUS_OK)
        status = sf_simple_command(CMD_CHIP_ERASE);
    if(status == SF_STATUS_OK)
        status = sf_wait_ready(SF_CHIP_TIMEOUT_MS);
    s_last_status = status;
}

void sf_PageWrite(uint8_t *data, uint32_t address, uint16_t length)
{
    (void)sf_program(address, data, length);
}

uint8_t sf_WriteBuffer(uint8_t *data, uint32_t address, uint32_t length)
{
    sf_status_t status = sf_program(address, data, length);
    if(status == SF_STATUS_OK)
        status = sf_verify(address, data, length);
    s_last_status = status;
    return status == SF_STATUS_OK ? 1U : 0U;
}

void sf_ReadBuffer(uint8_t *data, uint32_t address, uint32_t length)
{
    (void)sf_read(address, data, length);
}

void sf_ReadInfo(void)
{
    (void)sf_init();
}

void sfReadTest(void)
{
}
