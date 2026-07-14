#include "bsp.h"

#include <string.h>

#define BSP_SDIO_WIFI_INITIAL_CLOCK       400000U
#define BSP_SDIO_WIFI_COMMAND_TIMEOUT_MS      20U
#define BSP_SDIO_WIFI_POWER_TIMEOUT_MS      1000U

#define BSP_SDIO_WIFI_CMD_GO_IDLE_STATE        0U
#define BSP_SDIO_WIFI_CMD_IO_SEND_OP_COND       5U
#define BSP_SDIO_WIFI_CMD_SEND_RELATIVE_ADDR    3U
#define BSP_SDIO_WIFI_CMD_SELECT_CARD           7U
#define BSP_SDIO_WIFI_CMD_IO_RW_DIRECT         52U
#define BSP_SDIO_WIFI_CMD_IO_RW_EXTENDED       53U

#define BSP_SDIO_WIFI_TRANSFER_TIMEOUT_MS       500U
#define BSP_SDIO_WIFI_DMA_BUFFER_SIZE          2048U

#define BSP_SDIO_WIFI_OCR_READY          0x80000000U
#define BSP_SDIO_WIFI_OCR_VOLTAGE_MASK   0x00FFFF80U

static uint32_t last_status;
static volatile uint32_t oob_interrupt_count;
static bsp_sdio_wifi_oob_callback_t oob_callback;

__attribute__((section(".bss.sdio_dma"), aligned(32)))
static uint8_t sdio_dma_buffer[BSP_SDIO_WIFI_DMA_BUFFER_SIZE];

static HAL_StatusTypeDef bsp_sdio_wifi_clock_init(void);
static void bsp_sdio_wifi_gpio_init(void);
static HAL_StatusTypeDef bsp_sdio_wifi_send_command(uint32_t command_index,
                                                    uint32_t argument,
                                                    uint32_t response_type,
                                                    uint8_t response_has_crc,
                                                    uint32_t *response);
static HAL_StatusTypeDef bsp_sdio_wifi_read_direct(uint8_t function,
                                                  uint32_t address,
                                                  uint8_t *value);
static HAL_StatusTypeDef bsp_sdio_wifi_write_direct(uint8_t function,
                                                    uint32_t address,
                                                    uint8_t value);
static HAL_StatusTypeDef bsp_sdio_wifi_transfer_extended(uint8_t write,
                                                         uint8_t function,
                                                         uint32_t address,
                                                         uint8_t *data,
                                                         uint16_t data_size,
                                                         uint16_t function_block_size);
static uint16_t bsp_sdio_wifi_optimal_byte_block_size(uint16_t data_size);
static uint32_t bsp_sdio_wifi_data_block_size(uint16_t block_size);

HAL_StatusTypeDef bsp_sdio_wifi_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};
    HAL_StatusTypeDef status;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    oob_interrupt_count = 0U;

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    gpio_config.Pin = GPIO_PIN_13;
    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio_config);

    bsp_delay_ms(10U);
    bsp_sdio_wifi_gpio_init();

    status = bsp_sdio_wifi_clock_init();
    if (status != HAL_OK)
    {
        return status;
    }

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    bsp_delay_ms(200U);

    return HAL_OK;
}

HAL_StatusTypeDef bsp_sdio_wifi_probe(bsp_sdio_wifi_probe_result_t *result)
{
    HAL_StatusTypeDef status;
    uint32_t response = 0U;
    uint32_t start_cycles;
    uint32_t timeout_cycles;
    uint32_t voltage_request;
    uint8_t value;

    if (result == NULL)
    {
        return HAL_ERROR;
    }

    memset(result, 0, sizeof(*result));

    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_GO_IDLE_STATE,
                                        0U,
                                        SDMMC_RESPONSE_NO,
                                        0U,
                                        NULL);
    if (status != HAL_OK)
    {
        result->last_status = last_status;
        return status;
    }

    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_IO_SEND_OP_COND,
                                        0U,
                                        SDMMC_RESPONSE_SHORT,
                                        0U,
                                        &response);
    if (status != HAL_OK)
    {
        result->last_status = last_status;
        return status;
    }

    voltage_request = response & BSP_SDIO_WIFI_OCR_VOLTAGE_MASK;
    if (voltage_request == 0U)
    {
        voltage_request = BSP_SDIO_WIFI_OCR_VOLTAGE_MASK;
    }

    start_cycles = bsp_dwt_get_cycles();
    timeout_cycles = (SystemCoreClock / 1000U) * BSP_SDIO_WIFI_POWER_TIMEOUT_MS;
    do
    {
        status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_IO_SEND_OP_COND,
                                            voltage_request,
                                            SDMMC_RESPONSE_SHORT,
                                            0U,
                                            &response);
        if (status != HAL_OK)
        {
            result->last_status = last_status;
            return status;
        }

        if ((response & BSP_SDIO_WIFI_OCR_READY) != 0U)
        {
            break;
        }

        bsp_delay_ms(1U);
    } while (bsp_dwt_elapsed_cycles(start_cycles) < timeout_cycles);

    result->ocr = response;
    if ((response & BSP_SDIO_WIFI_OCR_READY) == 0U)
    {
        result->last_status = last_status;
        return HAL_TIMEOUT;
    }

    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_SEND_RELATIVE_ADDR,
                                        0U,
                                        SDMMC_RESPONSE_SHORT,
                                        1U,
                                        &response);
    if (status != HAL_OK)
    {
        result->last_status = last_status;
        return status;
    }
    result->relative_card_address = (uint16_t)(response >> 16);

    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_SELECT_CARD,
                                        (uint32_t)result->relative_card_address << 16,
                                        SDMMC_RESPONSE_SHORT,
                                        1U,
                                        &response);
    if (status != HAL_OK)
    {
        result->last_status = last_status;
        return status;
    }

    status = bsp_sdio_wifi_read_direct(0U, 0x00U, &value);
    if (status == HAL_OK)
    {
        result->cccr_revision = value;
        status = bsp_sdio_wifi_read_direct(0U, 0x01U, &value);
    }
    if (status == HAL_OK)
    {
        result->sd_revision = value;
        status = bsp_sdio_wifi_read_direct(0U, 0x02U, &value);
    }
    if (status == HAL_OK)
    {
        result->io_enable = value;
    }

    result->last_status = last_status;
    return status;
}

HAL_StatusTypeDef bsp_sdio_wifi_transfer(uint8_t write,
                                         uint8_t function,
                                         uint32_t address,
                                         uint8_t *data,
                                         uint16_t data_size,
                                         uint16_t function_block_size)
{
    if ((function > 7U) || (address > 0x1FFFFU) ||
        (data == NULL) || (data_size == 0U))
    {
        return HAL_ERROR;
    }

    if (data_size == 1U)
    {
        return (write != 0U) ?
            bsp_sdio_wifi_write_direct(function, address, *data) :
            bsp_sdio_wifi_read_direct(function, address, data);
    }

    return bsp_sdio_wifi_transfer_extended(write,
                                            function,
                                            address,
                                            data,
                                            data_size,
                                            function_block_size);
}

HAL_StatusTypeDef bsp_sdio_wifi_set_high_speed(void)
{
    SDMMC_InitTypeDef config = {0};
    uint32_t kernel_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    uint32_t divider;

    /* 16 MHz gives generous margin on the ART-Pi routing while bringing up. */
    divider = kernel_clock / (2U * 16000000U);
    if ((divider == 0U) || (divider >= 0x400U))
    {
        return HAL_ERROR;
    }

    config.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    config.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    config.BusWide = SDMMC_BUS_WIDE_4B;
    config.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    config.ClockDiv = divider;
    return SDMMC_Init(SDMMC2, config);
}

void bsp_sdio_wifi_set_power(uint8_t enabled)
{
    HAL_GPIO_WritePin(GPIOC,
                      GPIO_PIN_13,
                      (enabled != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void bsp_sdio_wifi_set_oob_callback(bsp_sdio_wifi_oob_callback_t callback)
{
    oob_callback = callback;
}

void bsp_sdio_wifi_enable_oob_interrupt(uint8_t enabled)
{
    if (enabled != 0U)
    {
        HAL_NVIC_EnableIRQ(EXTI3_IRQn);
        if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_SET)
        {
            __HAL_GPIO_EXTI_GENERATE_SWIT(GPIO_PIN_3);
        }
    }
    else
    {
        HAL_NVIC_DisableIRQ(EXTI3_IRQn);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
    }
}

void bsp_sdio_wifi_oob_irq_handler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_3) != 0U)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
        oob_interrupt_count++;
        if (oob_callback != NULL)
        {
            oob_callback();
        }
    }
}

uint32_t bsp_sdio_wifi_get_oob_interrupt_count(void)
{
    return oob_interrupt_count;
}

uint8_t bsp_sdio_wifi_get_oob_level(void)
{
    return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3) == GPIO_PIN_SET) ? 1U : 0U;
}

uint32_t bsp_sdio_wifi_get_clock(void)
{
    uint32_t kernel_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    uint32_t divider = READ_BIT(SDMMC2->CLKCR, SDMMC_CLKCR_CLKDIV);

    if (divider == 0U)
    {
        return 0U;
    }

    return kernel_clock / (2U * divider);
}

static HAL_StatusTypeDef bsp_sdio_wifi_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef clock_config = {0};
    SDMMC_InitTypeDef sdmmc_config = {0};
    uint32_t kernel_clock;
    uint32_t divider;

    clock_config.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    clock_config.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __HAL_RCC_SDMMC2_CLK_ENABLE();
    __HAL_RCC_SDMMC2_FORCE_RESET();
    __HAL_RCC_SDMMC2_RELEASE_RESET();

    kernel_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    divider = kernel_clock / (2U * BSP_SDIO_WIFI_INITIAL_CLOCK);
    if ((divider == 0U) || (divider >= 0x400U))
    {
        return HAL_ERROR;
    }

    sdmmc_config.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    sdmmc_config.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    sdmmc_config.BusWide = SDMMC_BUS_WIDE_1B;
    sdmmc_config.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    sdmmc_config.ClockDiv = divider;

    if (SDMMC_Init(SDMMC2, sdmmc_config) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (SDMMC_PowerState_ON(SDMMC2) != HAL_OK)
    {
        return HAL_ERROR;
    }

    WRITE_REG(SDMMC2->MASK, 0U);
    WRITE_REG(SDMMC2->ICR, SDMMC_STATIC_FLAGS);
    return HAL_OK;
}

static void bsp_sdio_wifi_gpio_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio_config.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_PULLUP;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_config.Alternate = GPIO_AF9_SDMMC2;
    HAL_GPIO_Init(GPIOB, &gpio_config);

    gpio_config.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio_config.Alternate = GPIO_AF11_SDMMC2;
    HAL_GPIO_Init(GPIOD, &gpio_config);

    gpio_config.Pin = GPIO_PIN_3;
    gpio_config.Mode = GPIO_MODE_IT_RISING;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_config.Alternate = 0U;
    HAL_GPIO_Init(GPIOE, &gpio_config);

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
    HAL_NVIC_SetPriority(EXTI3_IRQn, 6U, 0U);
    HAL_NVIC_DisableIRQ(EXTI3_IRQn);
}

static HAL_StatusTypeDef bsp_sdio_wifi_send_command(uint32_t command_index,
                                                    uint32_t argument,
                                                    uint32_t response_type,
                                                    uint8_t response_has_crc,
                                                    uint32_t *response)
{
    SDMMC_CmdInitTypeDef command = {0};
    uint32_t start_cycles;
    uint32_t timeout_cycles;
    uint32_t completion_flags;

    WRITE_REG(SDMMC2->ICR, SDMMC_STATIC_CMD_FLAGS);

    command.Argument = argument;
    command.CmdIndex = command_index;
    command.Response = response_type;
    command.WaitForInterrupt = SDMMC_WAIT_NO;
    command.CPSM = SDMMC_CPSM_ENABLE;

    if (SDMMC_SendCommand(SDMMC2, &command) != HAL_OK)
    {
        return HAL_ERROR;
    }

    completion_flags = (response_type == SDMMC_RESPONSE_NO) ?
        SDMMC_FLAG_CMDSENT :
        (SDMMC_FLAG_CMDREND | SDMMC_FLAG_CCRCFAIL | SDMMC_FLAG_CTIMEOUT);
    start_cycles = bsp_dwt_get_cycles();
    timeout_cycles = (SystemCoreClock / 1000U) * BSP_SDIO_WIFI_COMMAND_TIMEOUT_MS;

    do
    {
        last_status = READ_REG(SDMMC2->STA);
        if ((last_status & completion_flags) != 0U)
        {
            break;
        }
    } while (bsp_dwt_elapsed_cycles(start_cycles) < timeout_cycles);

    if ((last_status & completion_flags) == 0U)
    {
        return HAL_TIMEOUT;
    }
    if ((last_status & SDMMC_FLAG_CTIMEOUT) != 0U)
    {
        return HAL_TIMEOUT;
    }
    if ((response_has_crc != 0U) &&
        ((last_status & SDMMC_FLAG_CCRCFAIL) != 0U))
    {
        return HAL_ERROR;
    }

    if (response != NULL)
    {
        *response = SDMMC_GetResponse(SDMMC2, SDMMC_RESP1);
    }

    WRITE_REG(SDMMC2->ICR, SDMMC_STATIC_CMD_FLAGS);
    return HAL_OK;
}

static HAL_StatusTypeDef bsp_sdio_wifi_read_direct(uint8_t function,
                                                  uint32_t address,
                                                  uint8_t *value)
{
    uint32_t argument;
    uint32_t response;
    HAL_StatusTypeDef status;

    if ((function > 7U) || (address > 0x1FFFFU) || (value == NULL))
    {
        return HAL_ERROR;
    }

    argument = ((uint32_t)function << 28) | (address << 9);
    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_IO_RW_DIRECT,
                                        argument,
                                        SDMMC_RESPONSE_SHORT,
                                        1U,
                                        &response);
    if (status != HAL_OK)
    {
        return status;
    }

    if ((response & 0x0000CB00U) != 0U)
    {
        return HAL_ERROR;
    }

    *value = (uint8_t)response;
    return HAL_OK;
}

static HAL_StatusTypeDef bsp_sdio_wifi_write_direct(uint8_t function,
                                                    uint32_t address,
                                                    uint8_t value)
{
    uint32_t argument;
    uint32_t response;
    HAL_StatusTypeDef status;

    argument = 0x80000000U |
               ((uint32_t)function << 28) |
               (address << 9) |
               value;
    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_IO_RW_DIRECT,
                                        argument,
                                        SDMMC_RESPONSE_SHORT,
                                        1U,
                                        &response);
    if ((status != HAL_OK) || ((response & 0x0000CB00U) != 0U))
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef bsp_sdio_wifi_transfer_extended(uint8_t write,
                                                         uint8_t function,
                                                         uint32_t address,
                                                         uint8_t *data,
                                                         uint16_t data_size,
                                                         uint16_t function_block_size)
{
    SDMMC_DataInitTypeDef data_config = {0};
    HAL_StatusTypeDef status;
    uint16_t transfer_size;
    uint16_t data_block_size;
    uint32_t argument;
    uint32_t response;
    uint32_t start_cycles;
    uint32_t timeout_cycles;
    uint32_t error_flags;
    uint32_t completion_flags;
    uint32_t block_count;
    uint8_t block_mode;

    if (data_size > BSP_SDIO_WIFI_DMA_BUFFER_SIZE)
    {
        return HAL_ERROR;
    }

    if (data_size <= 512U)
    {
        data_block_size = bsp_sdio_wifi_optimal_byte_block_size(data_size);
        transfer_size = data_block_size;
        block_count = (transfer_size == 512U) ? 0U : transfer_size;
        block_mode = 0U;
    }
    else
    {
        if ((function_block_size == 0U) || (function_block_size > 512U))
        {
            return HAL_ERROR;
        }
        data_block_size = function_block_size;
        transfer_size = (uint16_t)(((uint32_t)data_size + data_block_size - 1U) /
                                   data_block_size * data_block_size);
        block_count = transfer_size / data_block_size;
        block_mode = 1U;
    }

    if ((transfer_size > sizeof(sdio_dma_buffer)) || (block_count > 511U))
    {
        return HAL_ERROR;
    }

    memset(sdio_dma_buffer, 0, transfer_size);
    if (write != 0U)
    {
        memcpy(sdio_dma_buffer, data, data_size);
    }

    WRITE_REG(SDMMC2->DCTRL, 0U);
    WRITE_REG(SDMMC2->IDMACTRL, SDMMC_DISABLE_IDMA);
    WRITE_REG(SDMMC2->ICR, SDMMC_STATIC_FLAGS);

    data_config.DataTimeOut = 0xFFFFFFFFU;
    data_config.DataLength = transfer_size;
    data_config.DataBlockSize = bsp_sdio_wifi_data_block_size(data_block_size);
    data_config.TransferDir = (write != 0U) ?
        SDMMC_TRANSFER_DIR_TO_CARD : SDMMC_TRANSFER_DIR_TO_SDMMC;
    data_config.TransferMode = SDMMC_TRANSFER_MODE_BLOCK;
    data_config.DPSM = SDMMC_DPSM_DISABLE;
    if (SDMMC_ConfigData(SDMMC2, &data_config) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __SDMMC_CMDTRANS_ENABLE(SDMMC2);
    WRITE_REG(SDMMC2->IDMABASE0, (uint32_t)sdio_dma_buffer);
    WRITE_REG(SDMMC2->IDMACTRL, SDMMC_ENABLE_IDMA_SINGLE_BUFF);

    argument = ((write != 0U) ? 0x80000000U : 0U) |
               ((uint32_t)function << 28) |
               ((block_mode != 0U) ? 0x08000000U : 0U) |
               0x04000000U |
               (address << 9) |
               block_count;

    status = bsp_sdio_wifi_send_command(BSP_SDIO_WIFI_CMD_IO_RW_EXTENDED,
                                        argument,
                                        SDMMC_RESPONSE_SHORT,
                                        1U,
                                        &response);
    if ((status != HAL_OK) || ((response & 0x0000CB00U) != 0U))
    {
        status = HAL_ERROR;
        goto transfer_exit;
    }

    error_flags = SDMMC_FLAG_DCRCFAIL | SDMMC_FLAG_DTIMEOUT |
                  SDMMC_FLAG_TXUNDERR | SDMMC_FLAG_RXOVERR |
                  SDMMC_FLAG_DABORT | SDMMC_FLAG_IDMATE;
    completion_flags = SDMMC_FLAG_DATAEND | error_flags;
    start_cycles = bsp_dwt_get_cycles();
    timeout_cycles = (SystemCoreClock / 1000U) * BSP_SDIO_WIFI_TRANSFER_TIMEOUT_MS;

    do
    {
        last_status = READ_REG(SDMMC2->STA);
        if ((last_status & completion_flags) != 0U)
        {
            break;
        }
    } while (bsp_dwt_elapsed_cycles(start_cycles) < timeout_cycles);

    if ((last_status & error_flags) != 0U)
    {
        status = HAL_ERROR;
    }
    else if ((last_status & SDMMC_FLAG_DATAEND) == 0U)
    {
        status = HAL_TIMEOUT;
    }
    else
    {
        status = HAL_OK;
        if (write == 0U)
        {
            memcpy(data, sdio_dma_buffer, data_size);
        }
    }

transfer_exit:
    WRITE_REG(SDMMC2->IDMACTRL, SDMMC_DISABLE_IDMA);
    __SDMMC_CMDTRANS_DISABLE(SDMMC2);
    WRITE_REG(SDMMC2->ICR, SDMMC_STATIC_FLAGS);
    return status;
}

static uint16_t bsp_sdio_wifi_optimal_byte_block_size(uint16_t data_size)
{
    uint16_t block_size = 4U;

    while ((block_size < data_size) && (block_size < 512U))
    {
        block_size <<= 1;
    }
    return block_size;
}

static uint32_t bsp_sdio_wifi_data_block_size(uint16_t block_size)
{
    switch (block_size)
    {
        case 1U: return SDMMC_DATABLOCK_SIZE_1B;
        case 2U: return SDMMC_DATABLOCK_SIZE_2B;
        case 4U: return SDMMC_DATABLOCK_SIZE_4B;
        case 8U: return SDMMC_DATABLOCK_SIZE_8B;
        case 16U: return SDMMC_DATABLOCK_SIZE_16B;
        case 32U: return SDMMC_DATABLOCK_SIZE_32B;
        case 64U: return SDMMC_DATABLOCK_SIZE_64B;
        case 128U: return SDMMC_DATABLOCK_SIZE_128B;
        case 256U: return SDMMC_DATABLOCK_SIZE_256B;
        case 512U: return SDMMC_DATABLOCK_SIZE_512B;
        default: return SDMMC_DATABLOCK_SIZE_4B;
    }
}
