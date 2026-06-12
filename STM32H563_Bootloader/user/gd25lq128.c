#include "gd25lq128.h"

#include "spi.h"
#include <string.h>

#define GD25_CMD_WRITE_ENABLE           0x06U
#define GD25_CMD_READ_STATUS1           0x05U
#define GD25_CMD_READ_DATA              0x03U
#define GD25_CMD_PAGE_PROGRAM           0x02U
#define GD25_CMD_SECTOR_ERASE_4K        0x20U
#define GD25_CMD_JEDEC_ID               0x9FU

#define GD25_STATUS_BUSY                0x01U
#define GD25_STATUS_WEL                 0x02U
#define GD25_SPI_TIMEOUT_MS             1000U
#define GD25_BUSY_TIMEOUT_MS            5000U
#define GD25_ERASE_TIMEOUT_MS           10000U

static void gd25_select(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

static void gd25_deselect(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static bool gd25_tx(const uint8_t *data, uint16_t len)
{
    return HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, GD25_SPI_TIMEOUT_MS) == HAL_OK;
}

static bool gd25_rx(uint8_t *data, uint16_t len)
{
    return HAL_SPI_Receive(&hspi1, data, len, GD25_SPI_TIMEOUT_MS) == HAL_OK;
}

static bool gd25_read_status1(uint8_t *status)
{
    uint8_t cmd = GD25_CMD_READ_STATUS1;
    bool ok;

    gd25_select();
    ok = gd25_tx(&cmd, 1U) && gd25_rx(status, 1U);
    gd25_deselect();

    return ok;
}

static bool gd25_wait_busy(uint32_t timeout_ms)
{
    uint32_t waited = 0U;
    uint8_t status = 0U;

    do
    {
        if(!gd25_read_status1(&status))
            return false;

        if((status & GD25_STATUS_BUSY) == 0U)
            return true;

        HAL_Delay(1U);
        waited++;
    } while(waited < timeout_ms);

    return false;
}

static bool gd25_write_enable(void)
{
    uint8_t cmd = GD25_CMD_WRITE_ENABLE;
    uint8_t status = 0U;

    gd25_select();
    if(!gd25_tx(&cmd, 1U))
    {
        gd25_deselect();
        return false;
    }
    gd25_deselect();

    if(!gd25_read_status1(&status))
        return false;

    return (status & GD25_STATUS_WEL) != 0U;
}

static bool gd25_addr_valid(uint32_t address, uint32_t len)
{
    return address < GD25LQ128_FLASH_SIZE_BYTES &&
           len <= GD25LQ128_FLASH_SIZE_BYTES &&
           address <= (GD25LQ128_FLASH_SIZE_BYTES - len);
}

bool gd25lq128_read_id(gd25lq128_id_t *id)
{
    uint8_t cmd = GD25_CMD_JEDEC_ID;
    uint8_t raw[3] = {0};
    bool ok;

    if(!id)
        return false;

    gd25_select();
    ok = gd25_tx(&cmd, 1U) && gd25_rx(raw, sizeof(raw));
    gd25_deselect();

    if(ok)
    {
        id->manufacturer_id = raw[0];
        id->memory_type = raw[1];
        id->capacity = raw[2];
    }

    return ok;
}

bool gd25lq128_read(uint32_t address, uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];
    bool ok;

    if(!data || len == 0U || !gd25_addr_valid(address, len))
        return false;

    cmd[0] = GD25_CMD_READ_DATA;
    cmd[1] = (uint8_t)(address >> 16);
    cmd[2] = (uint8_t)(address >> 8);
    cmd[3] = (uint8_t)address;

    gd25_select();
    ok = gd25_tx(cmd, sizeof(cmd)) && gd25_rx(data, (uint16_t)len);
    gd25_deselect();

    return ok;
}

bool gd25lq128_erase_4k(uint32_t address)
{
    uint8_t cmd[4];
    bool ok;

    if((address % GD25LQ128_SECTOR_SIZE_BYTES) != 0U || !gd25_addr_valid(address, GD25LQ128_SECTOR_SIZE_BYTES))
        return false;

    if(!gd25_wait_busy(GD25_BUSY_TIMEOUT_MS) || !gd25_write_enable())
        return false;

    cmd[0] = GD25_CMD_SECTOR_ERASE_4K;
    cmd[1] = (uint8_t)(address >> 16);
    cmd[2] = (uint8_t)(address >> 8);
    cmd[3] = (uint8_t)address;

    gd25_select();
    ok = gd25_tx(cmd, sizeof(cmd));
    gd25_deselect();

    return ok && gd25_wait_busy(GD25_ERASE_TIMEOUT_MS);
}

bool gd25lq128_page_program(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];
    bool ok;

    if(!data || len == 0U || len > GD25LQ128_PAGE_SIZE_BYTES || !gd25_addr_valid(address, len))
        return false;

    if(((address & (GD25LQ128_PAGE_SIZE_BYTES - 1U)) + len) > GD25LQ128_PAGE_SIZE_BYTES)
        return false;

    if(!gd25_wait_busy(GD25_BUSY_TIMEOUT_MS) || !gd25_write_enable())
        return false;

    cmd[0] = GD25_CMD_PAGE_PROGRAM;
    cmd[1] = (uint8_t)(address >> 16);
    cmd[2] = (uint8_t)(address >> 8);
    cmd[3] = (uint8_t)address;

    gd25_select();
    ok = gd25_tx(cmd, sizeof(cmd)) && gd25_tx(data, (uint16_t)len);
    gd25_deselect();

    return ok && gd25_wait_busy(GD25_BUSY_TIMEOUT_MS);
}

bool gd25lq128_write(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0U;

    if(!data || len == 0U || !gd25_addr_valid(address, len))
        return false;

    while(offset < len)
    {
        uint32_t page_room = GD25LQ128_PAGE_SIZE_BYTES - ((address + offset) & (GD25LQ128_PAGE_SIZE_BYTES - 1U));
        uint32_t chunk = (len - offset) < page_room ? (len - offset) : page_room;

        if(!gd25lq128_page_program(address + offset, &data[offset], chunk))
            return false;

        offset += chunk;
    }

    return true;
}

bool gd25lq128_read_verify(uint32_t address, const uint8_t *expected, uint32_t len)
{
    uint8_t buffer[64];
    uint32_t offset = 0U;

    if(!expected || len == 0U || !gd25_addr_valid(address, len))
        return false;

    while(offset < len)
    {
        uint32_t chunk = (len - offset) < sizeof(buffer) ? (len - offset) : sizeof(buffer);

        if(!gd25lq128_read(address + offset, buffer, chunk))
            return false;

        if(memcmp(buffer, &expected[offset], chunk) != 0)
            return false;

        offset += chunk;
    }

    return true;
}
