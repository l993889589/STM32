/**
 * @file drv_gd25lq128.c
 * @brief Bounded GD25LQ128 SPI NOR implementation.
 */

#include "drv_gd25lq128.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include "bsp_spi.h"
#include "bsp_time.h"

#define DRV_GD25LQ128_CMD_READ_ID       (0x9FU)
#define DRV_GD25LQ128_CMD_READ_STATUS_1 (0x05U)
#define DRV_GD25LQ128_CMD_WRITE_ENABLE  (0x06U)
#define DRV_GD25LQ128_CMD_READ          (0x03U)
#define DRV_GD25LQ128_CMD_PAGE_PROGRAM  (0x02U)
#define DRV_GD25LQ128_CMD_SECTOR_ERASE  (0x20U)
#define DRV_GD25LQ128_STATUS_BUSY       (0x01U)
#define DRV_GD25LQ128_JEDEC_CAPACITY    (0x18U)
#define DRV_GD25LQ128_SPI_TIMEOUT_MS    (20U)

static uint8_t drv_gd25lq128_is_initialized;

/**
 * @brief Write a command and optional 24-bit address while chip select is asserted.
 */
static bsp_status_t drv_gd25lq128_write_header(uint8_t command,
                                               uint32_t address,
                                               bool has_address)
{
    uint8_t header[4];
    uint32_t length = 1U;

    header[0] = command;
    if(has_address)
    {
        header[1] = (uint8_t)(address >> 16);
        header[2] = (uint8_t)(address >> 8);
        header[3] = (uint8_t)address;
        length = sizeof(header);
    }
    return bsp_spi_write(BOARD_SPI_FLASH,
                         header,
                         length,
                         DRV_GD25LQ128_SPI_TIMEOUT_MS);
}

/**
 * @brief Read status register 1 with one bounded SPI transaction.
 */
static bsp_status_t drv_gd25lq128_read_status(uint8_t *status_register)
{
    bsp_status_t status;

    if(status_register == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_spi_select(BOARD_SPI_FLASH, 1U);
    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_READ_STATUS_1,
                                            0U,
                                            false);
    }
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_read(BOARD_SPI_FLASH,
                              status_register,
                              1U,
                              DRV_GD25LQ128_SPI_TIMEOUT_MS);
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
    return status;
}

/**
 * @brief Wait until WIP clears or the caller's deadline expires.
 */
static bsp_status_t drv_gd25lq128_wait_ready(uint32_t timeout_ms)
{
    bsp_deadline_t deadline;
    uint8_t status_register;
    bsp_status_t status;

    if(timeout_ms == 0U)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    bsp_deadline_start(&deadline, timeout_ms);
    do
    {
        status = drv_gd25lq128_read_status(&status_register);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
        if((status_register & DRV_GD25LQ128_STATUS_BUSY) == 0U)
        {
            return BSP_STATUS_OK;
        }
        bsp_time_delay_ms(1U);
    } while(!bsp_deadline_has_expired(&deadline));

    return BSP_STATUS_TIMEOUT;
}

/**
 * @brief Set the flash write-enable latch.
 */
static bsp_status_t drv_gd25lq128_write_enable(void)
{
    bsp_status_t status = bsp_spi_select(BOARD_SPI_FLASH, 1U);

    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_WRITE_ENABLE,
                                            0U,
                                            false);
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
    return status;
}

/**
 * @brief Validate that an address range remains within the 16-MiB device.
 */
static bool drv_gd25lq128_range_is_valid(uint32_t address, uint32_t length)
{
    return (length > 0U) && (address < DRV_GD25LQ128_CAPACITY_BYTES) &&
           (length <= (DRV_GD25LQ128_CAPACITY_BYTES - address));
}

/**
 * @brief Implement drv_gd25lq128_init() as documented by its interface contract.
 */
bsp_status_t drv_gd25lq128_init(void)
{
    const bsp_spi_config_t config =
    {
        .baud_rate_hz = 12500000U,
        .clock_polarity = BSP_SPI_CLOCK_POLARITY_LOW,
        .clock_phase = BSP_SPI_CLOCK_PHASE_FIRST_EDGE
    };
    drv_gd25lq128_id_t identifier;
    bsp_status_t status;

    if(drv_gd25lq128_is_initialized != 0U)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    status = bsp_spi_init(BOARD_SPI_FLASH, &config);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    drv_gd25lq128_is_initialized = 1U;
    status = drv_gd25lq128_read_id(&identifier);
    if((status != BSP_STATUS_OK) ||
       (identifier.manufacturer_id == 0U) ||
       (identifier.manufacturer_id == UINT8_MAX) ||
       (identifier.capacity != DRV_GD25LQ128_JEDEC_CAPACITY))
    {
        drv_gd25lq128_is_initialized = 0U;
        return status == BSP_STATUS_OK ? BSP_STATUS_IO_ERROR : status;
    }
    return BSP_STATUS_OK;
}

/**
 * @brief Implement drv_gd25lq128_read_id() as documented by its interface contract.
 */
bsp_status_t drv_gd25lq128_read_id(drv_gd25lq128_id_t *identifier)
{
    uint8_t response[3];
    bsp_status_t status;

    if(identifier == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(drv_gd25lq128_is_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }

    status = bsp_spi_select(BOARD_SPI_FLASH, 1U);
    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_READ_ID,
                                            0U,
                                            false);
    }
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_read(BOARD_SPI_FLASH,
                              response,
                              sizeof(response),
                              DRV_GD25LQ128_SPI_TIMEOUT_MS);
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);

    if(status == BSP_STATUS_OK)
    {
        identifier->manufacturer_id = response[0];
        identifier->memory_type = response[1];
        identifier->capacity = response[2];
    }
    return status;
}

/**
 * @brief Implement drv_gd25lq128_read() as documented by its interface contract.
 */
bsp_status_t drv_gd25lq128_read(uint32_t address,
                                uint8_t *data,
                                uint32_t length,
                                uint32_t timeout_ms)
{
    uint32_t chunk;
    bsp_status_t status;

    if((drv_gd25lq128_is_initialized == 0U) || (data == NULL) ||
       !drv_gd25lq128_range_is_valid(address, length) || (timeout_ms == 0U))
    {
        return drv_gd25lq128_is_initialized == 0U ?
               BSP_STATUS_NOT_READY : BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_spi_select(BOARD_SPI_FLASH, 1U);
    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_READ,
                                            address,
                                            true);
    }
    while((status == BSP_STATUS_OK) && (length > 0U))
    {
        chunk = length > UINT16_MAX ? UINT16_MAX : length;
        status = bsp_spi_read(BOARD_SPI_FLASH, data, chunk, timeout_ms);
        data += chunk;
        length -= chunk;
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
    return status;
}

/**
 * @brief Program one fragment that does not cross a page boundary.
 */
static bsp_status_t drv_gd25lq128_program_page(uint32_t address,
                                               const uint8_t *data,
                                               uint32_t length,
                                               uint32_t timeout_ms)
{
    bsp_status_t status = drv_gd25lq128_write_enable();

    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_select(BOARD_SPI_FLASH, 1U);
    }
    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_PAGE_PROGRAM,
                                            address,
                                            true);
    }
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_write(BOARD_SPI_FLASH, data, length, timeout_ms);
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
    return status == BSP_STATUS_OK ?
           drv_gd25lq128_wait_ready(timeout_ms) : status;
}

/**
 * @brief Implement drv_gd25lq128_program() as documented by its interface contract.
 */
bsp_status_t drv_gd25lq128_program(uint32_t address,
                                   const uint8_t *data,
                                   uint32_t length,
                                   uint32_t timeout_ms)
{
    uint32_t chunk;
    uint32_t page_remaining;
    bsp_status_t status = BSP_STATUS_OK;

    if((drv_gd25lq128_is_initialized == 0U) || (data == NULL) ||
       !drv_gd25lq128_range_is_valid(address, length) || (timeout_ms == 0U))
    {
        return drv_gd25lq128_is_initialized == 0U ?
               BSP_STATUS_NOT_READY : BSP_STATUS_INVALID_ARGUMENT;
    }

    while((length > 0U) && (status == BSP_STATUS_OK))
    {
        page_remaining = DRV_GD25LQ128_PAGE_BYTES -
                         (address % DRV_GD25LQ128_PAGE_BYTES);
        chunk = length < page_remaining ? length : page_remaining;
        status = drv_gd25lq128_program_page(address,
                                            data,
                                            chunk,
                                            timeout_ms);
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    return status;
}

/**
 * @brief Implement drv_gd25lq128_erase_sector() as documented by its interface contract.
 */
bsp_status_t drv_gd25lq128_erase_sector(uint32_t address,
                                        uint32_t timeout_ms)
{
    bsp_status_t status;

    if(drv_gd25lq128_is_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((address >= DRV_GD25LQ128_CAPACITY_BYTES) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    address -= address % DRV_GD25LQ128_SECTOR_BYTES;

    status = drv_gd25lq128_write_enable();
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_select(BOARD_SPI_FLASH, 1U);
    }
    if(status == BSP_STATUS_OK)
    {
        status = drv_gd25lq128_write_header(DRV_GD25LQ128_CMD_SECTOR_ERASE,
                                            address,
                                            true);
    }
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
    return status == BSP_STATUS_OK ?
           drv_gd25lq128_wait_ready(timeout_ms) : status;
}
