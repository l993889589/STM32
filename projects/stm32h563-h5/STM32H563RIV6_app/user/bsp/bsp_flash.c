/**
 * @file bsp_flash.c
 * @brief BSP_FLASH external SPI NOR driver using the logical SPI bus.
 *
 * Purpose:
 *   Provides the board-level blocking read/erase/program primitives used by
 *   the UI asset A/B store and OTA storage code.
 *
 * Usage:
 *   Call bsp_flash_init() once after logical SPI initialization. All public
 *   APIs are synchronous and must be called from task or poll context, not
 *   from interrupts. Addresses are byte offsets inside the 16 MiB device.
 *
 * Constraints:
 *   Page program may only change erased bits from 1 to 0. Callers must erase
 *   affected 4 KiB sectors before writing. Reads are chunked and retried here
 *   because long blocking SPI transfers are more likely to fail on a busy
 *   display system than short transactions. Read commands are issued as a
 *   single full-duplex command+dummy transaction so the HAL state machine does
 *   not have to transition from transmit-only to receive-only while CS is low.
 *   A small OS abstraction lock serializes task-level callers that share SPI1
 *   between UI asset reads and OTA writes without adding an RTOS dependency
 *   to the BSP or device driver.
 */
#include "bsp_flash.h"

#include <string.h>

#include "bsp_spi.h"
#include "bsp_timer.h"
#include "osal_mutex.h"

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
#define GD25_READ_CHUNK_BYTES           128U
#define GD25_READ_RETRY_COUNT           5U
#define GD25_LOCK_TIMEOUT_MS            10000U

static uint8_t g_ready;
static osal_mutex_t g_mutex;

/** @brief Initialize the driver-local lock after the board SPI role is ready. */
bool bsp_flash_init(void)
{
    g_ready = osal_mutex_init(&g_mutex, "gd25_spi") ? 1U : 0U;
    return g_ready != 0U;
}

/** @brief Serialize one public flash operation with a bounded wait. */
static bool gd25_lock(uint32_t timeout_ms)
{
    return osal_mutex_lock(&g_mutex, timeout_ms);
}

/** @brief Release the driver operation lock. */
static void gd25_unlock(void)
{
    osal_mutex_unlock(&g_mutex);
}

/** @brief Assert the SPI NOR chip select. */
static void gd25_select(void)
{
    (void)bsp_spi_select(BOARD_SPI_FLASH, 1U);
}

/** @brief Release the SPI NOR chip select. */
static void gd25_deselect(void)
{
    (void)bsp_spi_select(BOARD_SPI_FLASH, 0U);
}

/** @brief Transmit one blocking SPI fragment. */
static bool gd25_tx(const uint8_t *data, uint16_t len)
{
    return g_ready != 0U &&
           bsp_spi_write(BOARD_SPI_FLASH,
                         data,
                         len,
                         GD25_SPI_TIMEOUT_MS) == BSP_STATUS_OK;
}

/** @brief Receive one blocking SPI fragment. */
static bool gd25_rx(uint8_t *data, uint16_t len)
{
    return g_ready != 0U &&
           bsp_spi_read(BOARD_SPI_FLASH,
                        data,
                        len,
                        GD25_SPI_TIMEOUT_MS) == BSP_STATUS_OK;
}

/** @brief Return SPI1 and the flash chip select to an idle state. */
static void gd25_recover_bus(void)
{
    gd25_deselect();
    (void)bsp_spi_abort(BOARD_SPI_FLASH);
    bsp_timer_delay_ms(1U);
}

/** @brief Read status register one without taking the public operation lock. */
static bool gd25_read_status1(uint8_t *status)
{
    uint8_t cmd = GD25_CMD_READ_STATUS1;
    bool ok;

    gd25_select();
    ok = gd25_tx(&cmd, 1U) && gd25_rx(status, 1U);
    gd25_deselect();

    return ok;
}

/** @brief Wait until the flash busy bit clears or the timeout expires. */
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

        bsp_timer_delay_ms(1U);
        waited++;
    } while(waited < timeout_ms);

    return false;
}

/** @brief Issue write-enable and verify that the WEL bit was accepted. */
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

/** @brief Validate one byte range against the physical flash capacity. */
static bool gd25_addr_valid(uint32_t address, uint32_t len)
{
    return address < BSP_FLASH_FLASH_SIZE_BYTES &&
           len <= BSP_FLASH_FLASH_SIZE_BYTES &&
           address <= (BSP_FLASH_FLASH_SIZE_BYTES - len);
}

/** @brief Read the JEDEC ID while the caller owns the operation lock. */
static bool gd25_read_id_unlocked(bsp_flash_id_t *id)
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

/** @brief Read the JEDEC manufacturer, type, and capacity identifiers. */
bool bsp_flash_read_id(bsp_flash_id_t *id)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_read_id_unlocked(id);
    gd25_unlock();
    return ok;
}

/** @brief Read and retry a range while the caller owns the operation lock. */
static bool gd25_read_unlocked(uint32_t address, uint8_t *data, uint32_t len)
{
    uint32_t offset = 0U;

    if(!data || len == 0U || !gd25_addr_valid(address, len))
        return false;

    while(offset < len)
    {
        uint32_t remain = len - offset;
        uint32_t chunk = remain > GD25_READ_CHUNK_BYTES ? GD25_READ_CHUNK_BYTES : remain;
        bool ok = false;

        for(uint32_t attempt = 0U; attempt < GD25_READ_RETRY_COUNT; attempt++)
        {
            uint8_t tx[4U + GD25_READ_CHUNK_BYTES];
            uint8_t rx[4U + GD25_READ_CHUNK_BYTES];
            uint16_t frame_len = (uint16_t)(chunk + 4U);

            if(!gd25_wait_busy(GD25_BUSY_TIMEOUT_MS))
            {
                gd25_recover_bus();
                continue;
            }

            memset(tx, 0xFF, frame_len);
            tx[0] = GD25_CMD_READ_DATA;
            tx[1] = (uint8_t)((address + offset) >> 16);
            tx[2] = (uint8_t)((address + offset) >> 8);
            tx[3] = (uint8_t)(address + offset);

            gd25_select();
            ok = g_ready != 0U &&
                 bsp_spi_transfer(BOARD_SPI_FLASH,
                                  tx,
                                  rx,
                                  frame_len,
                                  GD25_SPI_TIMEOUT_MS) == BSP_STATUS_OK;
            gd25_deselect();

            if(ok)
            {
                memcpy(&data[offset], &rx[4], chunk);
                break;
            }

            gd25_recover_bus();
        }

        if(!ok)
            return false;

        offset += chunk;
    }

    return true;
}

/** @brief Read a byte range with serialized access to the shared SPI bus. */
bool bsp_flash_read(uint32_t address, uint8_t *data, uint32_t len)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_read_unlocked(address, data, len);
    gd25_unlock();
    return ok;
}

/** @brief Erase one aligned sector while the caller owns the operation lock. */
static bool gd25_erase_4k_unlocked(uint32_t address)
{
    uint8_t cmd[4];
    bool ok;

    if((address % BSP_FLASH_SECTOR_SIZE_BYTES) != 0U || !gd25_addr_valid(address, BSP_FLASH_SECTOR_SIZE_BYTES))
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

/** @brief Erase one aligned 4 KiB sector with serialized access. */
bool bsp_flash_erase_4k(uint32_t address)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_erase_4k_unlocked(address);
    gd25_unlock();
    return ok;
}

/** @brief Program one page fragment while the caller owns the operation lock. */
static bool gd25_page_program_unlocked(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t cmd[4];
    bool ok;

    if(!data || len == 0U || len > BSP_FLASH_PAGE_SIZE_BYTES || !gd25_addr_valid(address, len))
        return false;

    if(((address & (BSP_FLASH_PAGE_SIZE_BYTES - 1U)) + len) > BSP_FLASH_PAGE_SIZE_BYTES)
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

/** @brief Program one fragment that does not cross a 256-byte page. */
bool bsp_flash_page_program(uint32_t address, const uint8_t *data, uint32_t len)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_page_program_unlocked(address, data, len);
    gd25_unlock();
    return ok;
}

/** @brief Split and program a range while the caller owns the operation lock. */
static bool gd25_write_unlocked(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0U;

    if(!data || len == 0U || !gd25_addr_valid(address, len))
        return false;

    while(offset < len)
    {
        uint32_t page_room = BSP_FLASH_PAGE_SIZE_BYTES - ((address + offset) & (BSP_FLASH_PAGE_SIZE_BYTES - 1U));
        uint32_t chunk = (len - offset) < page_room ? (len - offset) : page_room;

        if(!gd25_page_program_unlocked(address + offset, &data[offset], chunk))
            return false;

        offset += chunk;
    }

    return true;
}

/** @brief Program an arbitrary byte range with serialized access. */
bool bsp_flash_write(uint32_t address, const uint8_t *data, uint32_t len)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_write_unlocked(address, data, len);
    gd25_unlock();
    return ok;
}

/** @brief Compare a flash range while the caller owns the operation lock. */
static bool gd25_read_verify_unlocked(uint32_t address, const uint8_t *expected, uint32_t len)
{
    uint8_t buffer[64];
    uint32_t offset = 0U;

    if(!expected || len == 0U || !gd25_addr_valid(address, len))
        return false;

    while(offset < len)
    {
        uint32_t chunk = (len - offset) < sizeof(buffer) ? (len - offset) : sizeof(buffer);

        if(!gd25_read_unlocked(address + offset, buffer, chunk))
            return false;

        if(memcmp(buffer, &expected[offset], chunk) != 0)
            return false;

        offset += chunk;
    }

    return true;
}

/** @brief Read back and verify a flash byte range. */
bool bsp_flash_read_verify(uint32_t address, const uint8_t *expected, uint32_t len)
{
    bool ok;

    if(!gd25_lock(GD25_LOCK_TIMEOUT_MS))
        return false;

    ok = gd25_read_verify_unlocked(address, expected, len);
    gd25_unlock();
    return ok;
}
