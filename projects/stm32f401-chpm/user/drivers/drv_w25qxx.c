/**
 * @file drv_w25qxx.c
 * @brief PA4-select W25Q64 flash device over the logical SPI BSP.
 */

#include "drv_w25qxx.h"

#include <string.h>

#include "bsp_spi.h"
#include "stm32f4xx_hal.h"

#define SF_CS_PORT             GPIOA
#define SF_CS_PIN              GPIO_PIN_4
#define SF_IO_TIMEOUT_MS       (10U)
#define SF_PAGE_TIMEOUT_MS     (10U)
#define SF_SECTOR_TIMEOUT_MS   (500U)
#define SF_CHIP_TIMEOUT_MS     (10000U)

#define CMD_READ_STATUS        (0x05U)
#define CMD_WRITE_ENABLE       (0x06U)
#define CMD_PAGE_PROGRAM       (0x02U)
#define CMD_READ               (0x03U)
#define CMD_SECTOR_ERASE       (0x20U)
#define CMD_CHIP_ERASE         (0xC7U)
#define CMD_READ_ID            (0x9FU)
#define STATUS_BUSY            (0x01U)

SFLASH_T g_tSF;

static sf_status_t g_last_status = SF_STATUS_NOT_READY;
static sf_wait_hook_t g_wait_hook;

/** @brief Install an optional non-blocking hook used between busy polls. */
void sf_set_wait_hook(sf_wait_hook_t hook)
{
    g_wait_hook = hook;
}

/** @brief Map the shared BSP status domain into the flash API. */
static sf_status_t sf_map_status(bsp_status_t status)
{
    if(status == BSP_STATUS_OK)
        return SF_STATUS_OK;
    if(status == BSP_STATUS_TIMEOUT)
        return SF_STATUS_TIMEOUT;
    if(status == BSP_STATUS_NOT_READY)
        return SF_STATUS_NOT_READY;
    if(status == BSP_STATUS_INVALID_ARGUMENT)
        return SF_STATUS_INVALID_ARGUMENT;
    return SF_STATUS_IO_ERROR;
}

/** @brief Acquire SPI1 and assert the private PA4 chip select. */
static sf_status_t sf_select(void)
{
    bsp_status_t status = bsp_spi_acquire();

    if(status != BSP_STATUS_OK)
        return sf_map_status(status);
    HAL_GPIO_WritePin(SF_CS_PORT, SF_CS_PIN, GPIO_PIN_RESET);
    return SF_STATUS_OK;
}

/** @brief Deassert PA4 before releasing the shared SPI1 bus. */
static void sf_deselect(void)
{
    HAL_GPIO_WritePin(SF_CS_PORT, SF_CS_PIN, GPIO_PIN_SET);
    bsp_spi_release();
}

/** @brief Execute an opcode without payload. */
static sf_status_t sf_simple_command(uint8_t command)
{
    sf_status_t status = sf_select();

    if(status != SF_STATUS_OK)
        return status;
    status = sf_map_status(bsp_spi_transfer(&command,
                                            NULL,
                                            1U,
                                            SF_IO_TIMEOUT_MS));
    sf_deselect();
    return status;
}

/** @brief Read status register 1. */
static sf_status_t sf_read_status(uint8_t *status_register)
{
    uint8_t command[2] = {CMD_READ_STATUS, 0xFFU};
    uint8_t response[2];
    sf_status_t status;

    if(status_register == NULL)
        return SF_STATUS_INVALID_ARGUMENT;
    status = sf_select();
    if(status != SF_STATUS_OK)
        return status;
    status = sf_map_status(bsp_spi_transfer(command,
                                            response,
                                            sizeof(command),
                                            SF_IO_TIMEOUT_MS));
    sf_deselect();
    if(status == SF_STATUS_OK)
        *status_register = response[1];
    return status;
}

/** @brief Poll the device busy bit until ready or a physical timeout expires. */
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
        /*
         * The device layer remains RTOS-neutral.  A ThreadX application may
         * install a one-tick sleep hook so long erase cycles do not spin.
         */
        if(g_wait_hook != NULL)
            g_wait_hook();
    } while((HAL_GetTick() - start) < timeout_ms);
    return SF_STATUS_TIMEOUT;
}

/** @brief Set the write-enable latch before an erase or program operation. */
static sf_status_t sf_write_enable(void)
{
    return sf_simple_command(CMD_WRITE_ENABLE);
}

/**
 * @brief Verify that one aligned 4 KiB sector contains only erased bytes.
 * @param address Any address inside the sector to verify.
 * @return OK only when every byte reads as 0xFF.
 */
static sf_status_t sf_verify_erased_sector(uint32_t address)
{
    uint8_t data[64];
    sf_status_t status;

    address &= ~(W25Q64_SECTOR_SIZE - 1U);
    for(size_t offset = 0U; offset < W25Q64_SECTOR_SIZE;
        offset += sizeof(data))
    {
        status = sf_read(address + (uint32_t)offset, data, sizeof(data));
        if(status != SF_STATUS_OK)
            return status;
        for(size_t index = 0U; index < sizeof(data); index++)
        {
            if(data[index] != 0xFFU)
                return SF_STATUS_VERIFY_FAILED;
        }
    }
    return SF_STATUS_OK;
}

/** @brief Program one page-contained data span. */
static sf_status_t sf_program_page(uint32_t address,
                                   const uint8_t *data,
                                   size_t length)
{
    uint8_t command[4];
    sf_status_t status;

    if(data == NULL || length == 0U || length > W25Q64_PAGE_SIZE ||
       ((address & (W25Q64_PAGE_SIZE - 1U)) + length) > W25Q64_PAGE_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;
    status = sf_write_enable();
    if(status != SF_STATUS_OK)
        return status;

    command[0] = CMD_PAGE_PROGRAM;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;
    status = sf_select();
    if(status != SF_STATUS_OK)
        return status;
    status = sf_map_status(bsp_spi_transfer(command,
                                            NULL,
                                            sizeof(command),
                                            SF_IO_TIMEOUT_MS));
    if(status == SF_STATUS_OK)
    {
        status = sf_map_status(bsp_spi_transfer(data,
                                                NULL,
                                                length,
                                                SF_IO_TIMEOUT_MS));
    }
    sf_deselect();
    return status == SF_STATUS_OK ? sf_wait_ready(SF_PAGE_TIMEOUT_MS) :
           status;
}

/** @brief Initialize PA4 and verify the expected JEDEC identity. */
sf_status_t sf_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t id;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = SF_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SF_CS_PORT, &gpio);
    HAL_GPIO_WritePin(SF_CS_PORT, SF_CS_PIN, GPIO_PIN_SET);

    id = sf_ReadID();
    memset(&g_tSF, 0, sizeof(g_tSF));
    g_tSF.ChipID = id;
    if(id != W25Q64_JEDEC_ID)
    {
        g_last_status = SF_STATUS_NOT_READY;
        return g_last_status;
    }
    memcpy(g_tSF.ChipName, "W25Q64JV", 9U);
    g_tSF.TotalSize = W25Q64_TOTAL_SIZE;
    g_tSF.SectorSize = (uint16_t)W25Q64_SECTOR_SIZE;
    g_last_status = SF_STATUS_OK;
    return g_last_status;
}

/** @brief Read the three-byte JEDEC manufacturer/type/capacity value. */
uint32_t sf_ReadID(void)
{
    uint8_t command[4] = {CMD_READ_ID, 0xFFU, 0xFFU, 0xFFU};
    uint8_t response[4];
    sf_status_t status = sf_select();
    uint32_t id = 0U;

    if(status == SF_STATUS_OK)
    {
        status = sf_map_status(bsp_spi_transfer(command,
                                                response,
                                                sizeof(command),
                                                SF_IO_TIMEOUT_MS));
        sf_deselect();
    }
    if(status == SF_STATUS_OK)
    {
        id = ((uint32_t)response[1] << 16) |
             ((uint32_t)response[2] << 8) |
             response[3];
    }
    g_last_status = status;
    return id;
}

/** @brief Read an arbitrary validated flash address range. */
sf_status_t sf_read(uint32_t address, void *data, size_t length)
{
    uint8_t command[4];
    sf_status_t status;

    if((data == NULL && length != 0U) || address > W25Q64_TOTAL_SIZE ||
       length > W25Q64_TOTAL_SIZE - address)
        return SF_STATUS_INVALID_ARGUMENT;
    if(length == 0U)
        return SF_STATUS_OK;

    command[0] = CMD_READ;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;
    status = sf_select();
    if(status != SF_STATUS_OK)
        return status;
    status = sf_map_status(bsp_spi_transfer(command,
                                            NULL,
                                            sizeof(command),
                                            SF_IO_TIMEOUT_MS));
    if(status == SF_STATUS_OK)
    {
        status = sf_map_status(bsp_spi_transfer(NULL,
                                                (uint8_t *)data,
                                                length,
                                                SF_IO_TIMEOUT_MS));
    }
    sf_deselect();
    g_last_status = status;
    return status;
}

/** @brief Erase the 4 KiB sector containing the supplied address. */
sf_status_t sf_erase_sector_checked(uint32_t address)
{
    uint8_t command[4];
    sf_status_t status;

    if(address >= W25Q64_TOTAL_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;
    address &= ~(W25Q64_SECTOR_SIZE - 1U);
    status = sf_write_enable();
    if(status != SF_STATUS_OK)
        return status;
    command[0] = CMD_SECTOR_ERASE;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;
    status = sf_select();
    if(status == SF_STATUS_OK)
    {
        status = sf_map_status(bsp_spi_transfer(command,
                                                NULL,
                                                sizeof(command),
                                                SF_IO_TIMEOUT_MS));
        sf_deselect();
    }
    if(status == SF_STATUS_OK)
        status = sf_wait_ready(SF_SECTOR_TIMEOUT_MS);
    if(status == SF_STATUS_OK)
        status = sf_verify_erased_sector(address);
    g_last_status = status;
    return status;
}

/** @brief Program an arbitrary range by splitting it at page boundaries. */
sf_status_t sf_program(uint32_t address, const void *data, size_t length)
{
    const uint8_t *input = (const uint8_t *)data;
    sf_status_t status = SF_STATUS_OK;

    if((data == NULL && length != 0U) || address > W25Q64_TOTAL_SIZE ||
       length > W25Q64_TOTAL_SIZE - address)
        return SF_STATUS_INVALID_ARGUMENT;
    while(length != 0U)
    {
        size_t page_space =
            W25Q64_PAGE_SIZE - (address & (W25Q64_PAGE_SIZE - 1U));
        size_t chunk = length < page_space ? length : page_space;

        status = sf_program_page(address, input, chunk);
        if(status != SF_STATUS_OK)
            break;
        address += (uint32_t)chunk;
        input += chunk;
        length -= chunk;
    }
    g_last_status = status;
    return status;
}

/** @brief Read back and compare a programmed range in bounded chunks. */
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

/** @brief Return the most recent device-operation status. */
sf_status_t sf_last_status(void)
{
    return g_last_status;
}

/** @brief Compatibility wrapper for legacy startup code. */
void bsp_InitSFlash(void)
{
    (void)sf_init();
}

/** @brief Compatibility wrapper for sector erase. */
void sf_EraseSector(uint32_t address)
{
    (void)sf_erase_sector_checked(address);
}

/** @brief Erase the entire chip with the device maximum timeout. */
void sf_EraseChip(void)
{
    sf_status_t status = sf_write_enable();

    if(status == SF_STATUS_OK)
        status = sf_simple_command(CMD_CHIP_ERASE);
    if(status == SF_STATUS_OK)
        status = sf_wait_ready(SF_CHIP_TIMEOUT_MS);
    g_last_status = status;
}

/** @brief Compatibility wrapper for page programming. */
void sf_PageWrite(uint8_t *data, uint32_t address, uint16_t length)
{
    (void)sf_program(address, data, length);
}

/** @brief Program and verify a legacy caller buffer. */
uint8_t sf_WriteBuffer(uint8_t *data, uint32_t address, uint32_t length)
{
    sf_status_t status = sf_program(address, data, length);

    if(status == SF_STATUS_OK)
        status = sf_verify(address, data, length);
    g_last_status = status;
    return status == SF_STATUS_OK ? 1U : 0U;
}

/** @brief Compatibility wrapper for arbitrary reads. */
void sf_ReadBuffer(uint8_t *data, uint32_t address, uint32_t length)
{
    (void)sf_read(address, data, length);
}

/** @brief Refresh legacy flash information. */
void sf_ReadInfo(void)
{
    (void)sf_init();
}

/** @brief Retained no-op test hook. */
void sfReadTest(void)
{
}
