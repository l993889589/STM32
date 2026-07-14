#include "bsp.h"

#define BSP_W25Q128_CS_PORT               GPIOA
#define BSP_W25Q128_CS_PIN                GPIO_PIN_4

#define BSP_W25Q128_SPI_PRESCALER         SPI_BAUDRATEPRESCALER_2
#define BSP_W25Q128_SPI_PHASE             SPI_PHASE_1EDGE
#define BSP_W25Q128_SPI_POLARITY          SPI_POLARITY_LOW

#define BSP_W25Q128_COMMAND_WRITE_ENABLE  0x06U
#define BSP_W25Q128_COMMAND_READ_STATUS   0x05U
#define BSP_W25Q128_COMMAND_READ          0x03U
#define BSP_W25Q128_COMMAND_PAGE_PROGRAM  0x02U
#define BSP_W25Q128_COMMAND_SECTOR_ERASE  0x20U
#define BSP_W25Q128_COMMAND_READ_ID       0x9FU

#define BSP_W25Q128_STATUS_BUSY           0x01U
#define BSP_W25Q128_PAGE_TIMEOUT_MS       100U
#define BSP_W25Q128_SECTOR_TIMEOUT_MS     5000U

static HAL_StatusTypeDef bsp_w25q128_select(void);
static void bsp_w25q128_deselect(void);
static HAL_StatusTypeDef bsp_w25q128_write_enable(void);
static HAL_StatusTypeDef bsp_w25q128_wait_ready(uint32_t timeout_ms);
static HAL_StatusTypeDef bsp_w25q128_page_program(uint32_t address,
                                                  const uint8_t *data,
                                                  uint16_t length);

HAL_StatusTypeDef bsp_w25q128_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};
    uint32_t jedec_id;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(BSP_W25Q128_CS_PORT,
                      BSP_W25Q128_CS_PIN,
                      GPIO_PIN_SET);

    gpio_config.Pin = BSP_W25Q128_CS_PIN;
    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(BSP_W25Q128_CS_PORT, &gpio_config);

    if (bsp_w25q128_read_id(&jedec_id) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return (jedec_id == BSP_W25Q128_JEDEC_ID) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef bsp_w25q128_read_id(uint32_t *jedec_id)
{
    uint8_t tx_data[4] =
    {
        BSP_W25Q128_COMMAND_READ_ID,
        BSP_SPI_DUMMY_BYTE,
        BSP_SPI_DUMMY_BYTE,
        BSP_SPI_DUMMY_BYTE
    };
    uint8_t rx_data[4];
    HAL_StatusTypeDef status;

    if (jedec_id == NULL)
    {
        return HAL_ERROR;
    }

    status = bsp_w25q128_select();
    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(tx_data,
                                  rx_data,
                                  sizeof(tx_data),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    bsp_w25q128_deselect();

    if (status == HAL_OK)
    {
        *jedec_id = ((uint32_t)rx_data[1] << 16) |
                    ((uint32_t)rx_data[2] << 8) |
                    (uint32_t)rx_data[3];
    }

    return status;
}

HAL_StatusTypeDef bsp_w25q128_read(uint32_t address,
                                   uint8_t *data,
                                   size_t length)
{
    uint8_t command[4];
    HAL_StatusTypeDef status;

    if (length == 0U)
    {
        return HAL_OK;
    }

    if ((data == NULL) ||
        (address >= BSP_W25Q128_TOTAL_SIZE) ||
        (length > (size_t)(BSP_W25Q128_TOTAL_SIZE - address)))
    {
        return HAL_ERROR;
    }

    command[0] = BSP_W25Q128_COMMAND_READ;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;

    status = bsp_w25q128_select();
    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(command,
                                  NULL,
                                  sizeof(command),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        status = bsp_spi_bus_transfer(NULL,
                                      data,
                                      length,
                                      BSP_SPI_DEFAULT_TIMEOUT_MS);
    }

    bsp_w25q128_deselect();
    return status;
}

HAL_StatusTypeDef bsp_w25q128_write(uint32_t address,
                                    const uint8_t *data,
                                    size_t length)
{
    size_t offset = 0U;

    if (length == 0U)
    {
        return HAL_OK;
    }

    if ((data == NULL) ||
        (address >= BSP_W25Q128_TOTAL_SIZE) ||
        (length > (size_t)(BSP_W25Q128_TOTAL_SIZE - address)))
    {
        return HAL_ERROR;
    }

    while (offset < length)
    {
        uint32_t current_address = address + (uint32_t)offset;
        uint32_t page_offset = current_address & (BSP_W25Q128_PAGE_SIZE - 1UL);
        size_t page_space = BSP_W25Q128_PAGE_SIZE - page_offset;
        size_t remaining = length - offset;
        uint16_t chunk_length = (remaining > page_space) ?
                                (uint16_t)page_space :
                                (uint16_t)remaining;
        HAL_StatusTypeDef status = bsp_w25q128_page_program(current_address,
                                                            &data[offset],
                                                            chunk_length);

        if (status != HAL_OK)
        {
            return status;
        }

        offset += chunk_length;
    }

    return HAL_OK;
}

HAL_StatusTypeDef bsp_w25q128_erase_sector(uint32_t address)
{
    uint8_t command[4];
    HAL_StatusTypeDef status;

    if (address >= BSP_W25Q128_TOTAL_SIZE)
    {
        return HAL_ERROR;
    }

    address &= ~(BSP_W25Q128_SECTOR_SIZE - 1UL);
    status = bsp_w25q128_write_enable();
    if (status != HAL_OK)
    {
        return status;
    }

    command[0] = BSP_W25Q128_COMMAND_SECTOR_ERASE;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;

    status = bsp_w25q128_select();
    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(command,
                                  NULL,
                                  sizeof(command),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    bsp_w25q128_deselect();

    if (status != HAL_OK)
    {
        return status;
    }

    return bsp_w25q128_wait_ready(BSP_W25Q128_SECTOR_TIMEOUT_MS);
}

HAL_StatusTypeDef bsp_w25q128_read_status(uint8_t *status_register)
{
    uint8_t tx_data[2] =
    {
        BSP_W25Q128_COMMAND_READ_STATUS,
        BSP_SPI_DUMMY_BYTE
    };
    uint8_t rx_data[2];
    HAL_StatusTypeDef status;

    if (status_register == NULL)
    {
        return HAL_ERROR;
    }

    status = bsp_w25q128_select();
    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(tx_data,
                                  rx_data,
                                  sizeof(tx_data),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    bsp_w25q128_deselect();

    if (status == HAL_OK)
    {
        *status_register = rx_data[1];
    }

    return status;
}

static HAL_StatusTypeDef bsp_w25q128_select(void)
{
    HAL_StatusTypeDef status = bsp_spi_bus_enter();

    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_config(BSP_W25Q128_SPI_PRESCALER,
                                BSP_W25Q128_SPI_PHASE,
                                BSP_W25Q128_SPI_POLARITY);
    if (status != HAL_OK)
    {
        bsp_spi_bus_exit();
        return status;
    }

    HAL_GPIO_WritePin(BSP_W25Q128_CS_PORT,
                      BSP_W25Q128_CS_PIN,
                      GPIO_PIN_RESET);
    return HAL_OK;
}

static void bsp_w25q128_deselect(void)
{
    HAL_GPIO_WritePin(BSP_W25Q128_CS_PORT,
                      BSP_W25Q128_CS_PIN,
                      GPIO_PIN_SET);
    bsp_spi_bus_exit();
}

static HAL_StatusTypeDef bsp_w25q128_write_enable(void)
{
    uint8_t command = BSP_W25Q128_COMMAND_WRITE_ENABLE;
    HAL_StatusTypeDef status = bsp_w25q128_select();

    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(&command,
                                  NULL,
                                  sizeof(command),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    bsp_w25q128_deselect();
    return status;
}

static HAL_StatusTypeDef bsp_w25q128_wait_ready(uint32_t timeout_ms)
{
    uint32_t elapsed_ms;

    for (elapsed_ms = 0U; elapsed_ms <= timeout_ms; elapsed_ms++)
    {
        uint8_t status_register;
        HAL_StatusTypeDef status = bsp_w25q128_read_status(&status_register);

        if (status != HAL_OK)
        {
            return status;
        }

        if ((status_register & BSP_W25Q128_STATUS_BUSY) == 0U)
        {
            return HAL_OK;
        }

        if (elapsed_ms < timeout_ms)
        {
            bsp_delay_ms(1U);
        }
    }

    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef bsp_w25q128_page_program(uint32_t address,
                                                  const uint8_t *data,
                                                  uint16_t length)
{
    uint8_t command[4];
    HAL_StatusTypeDef status;

    if ((data == NULL) || (length == 0U) ||
        (length > BSP_W25Q128_PAGE_SIZE) ||
        (((address & (BSP_W25Q128_PAGE_SIZE - 1UL)) + length) >
         BSP_W25Q128_PAGE_SIZE))
    {
        return HAL_ERROR;
    }

    status = bsp_w25q128_write_enable();
    if (status != HAL_OK)
    {
        return status;
    }

    command[0] = BSP_W25Q128_COMMAND_PAGE_PROGRAM;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;

    status = bsp_w25q128_select();
    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_spi_bus_transfer(command,
                                  NULL,
                                  sizeof(command),
                                  BSP_SPI_DEFAULT_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        status = bsp_spi_bus_transfer(data,
                                      NULL,
                                      length,
                                      BSP_SPI_DEFAULT_TIMEOUT_MS);
    }

    bsp_w25q128_deselect();
    if (status != HAL_OK)
    {
        return status;
    }

    return bsp_w25q128_wait_ready(BSP_W25Q128_PAGE_TIMEOUT_MS);
}
