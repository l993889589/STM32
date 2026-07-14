#include "ap6212_wiced.h"
#include "bsp.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define AP6212_WIFI_IMAGE_SIZE 355159U
#define AP6212_SDIO_RETRY_COUNT     3U

static uint16_t function_block_sizes[3] = {64U, 64U, 512U};
static uint32_t sdio_transfer_count;
static uint32_t resource_read_count;
static bsp_sdio_wifi_probe_result_t wiced_probe_result;
static uint8_t wiced_bus_enumerated;

static const char wifi_nvram_image[] =
    "manfid=0x2d0" "\x00"
    "prodid=0x0726" "\x00"
    "vendid=0x14e4" "\x00"
    "devid=0x43e2" "\x00"
    "boardtype=0x0726" "\x00"
    "boardrev=0x1101" "\x00"
    "boardnum=22" "\x00"
    "xtalfreq=26000" "\x00"
    "sromrev=11" "\x00"
    "boardflags=0x00404201" "\x00"
    "boardflags3=0x08000000" "\x00"
    "macaddr=02:0A:F7:fe:86:1c" "\x00"
    "nocrc=1" "\x00"
    "ag0=255" "\x00"
    "aa2g=1" "\x00"
    "ccode=ALL" "\x00"
    "swdiv_en=1" "\x00"
    "swdiv_gpio=2" "\x00"
    "pa0itssit=0x20" "\x00"
    "extpagain2g=0" "\x00"
    "pa2ga0=-215,5267,-656" "\x00"
    "AvVmid_c0=0x0,0xc8" "\x00"
    "cckpwroffset0=5" "\x00"
    "maxp2ga0=80" "\x00"
    "txpwrbckof=6" "\x00"
    "cckbw202gpo=0x6666" "\x00"
    "legofdmbw202gpo=0xaaaaaaaa" "\x00"
    "mcsbw202gpo=0xbbbbbbbb" "\x00"
    "propbw202gpo=0xdd" "\x00"
    "ofdmdigfilttype=18" "\x00"
    "ofdmdigfilttypebe=18" "\x00"
    "papdmode=1" "\x00"
    "papdvalidtest=1" "\x00"
    "pacalidx2g=32" "\x00"
    "papdepsoffset=-36" "\x00"
    "papdendidx=61" "\x00"
    "wl0id=0x431b" "\x00"
    "deadman_to=0xffffffff" "\x00"
    "muxenab=0x11" "\x00"
    "spurconfig=0x3" "\x00"
    "\x00\x00";

wwd_result_t host_platform_bus_init(void)
{
    HAL_StatusTypeDef status;

    bsp_uart_write_string(BSP_UART_DEBUG, "WICED bus_init: begin\r\n");

    wiced_bus_enumerated = 0U;
    memset(&wiced_probe_result, 0, sizeof(wiced_probe_result));
    status = bsp_sdio_wifi_init();
    if (status == HAL_OK)
    {
        status = bsp_sdio_wifi_probe(&wiced_probe_result);
    }
    if (status != HAL_OK)
    {
        bsp_uart_write_string(BSP_UART_DEBUG, "WICED bus_init: enumerate failed\r\n");
        return 1022;
    }
    wiced_bus_enumerated = 1U;
    sdio_transfer_count = 0U;
    return WWD_SUCCESS;
}

wwd_result_t host_platform_sdio_enumerate(void)
{
    char message[128];

    (void)snprintf(message,
                   sizeof(message),
                   "WICED enumerate: cached=%u RCA=%04X OCR=%08lX\r\n",
                   (unsigned int)wiced_bus_enumerated,
                   (unsigned int)wiced_probe_result.relative_card_address,
                   (unsigned long)wiced_probe_result.ocr);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    return (wiced_bus_enumerated != 0U) ? WWD_SUCCESS : 1022;
}

wwd_result_t host_platform_bus_deinit(void)
{
    bsp_sdio_wifi_enable_oob_interrupt(0U);
    bsp_sdio_wifi_set_power(0U);
    return WWD_SUCCESS;
}

wwd_result_t host_platform_sdio_transfer(int32_t direction,
                                         int32_t function,
                                         uint32_t address,
                                         uint16_t data_size,
                                         uint8_t *data,
                                         int32_t response_expected)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    uint16_t block_size;
    uint32_t retry;

    (void)response_expected;

    if ((function < 0) || (function > 2) ||
        (data == NULL) || (data_size == 0U))
    {
        return WWD_SDIO_RETRIES_EXCEEDED;
    }

    block_size = function_block_sizes[function];
    for (retry = 0U; retry < AP6212_SDIO_RETRY_COUNT; retry++)
    {
        status = bsp_sdio_wifi_transfer((direction != 0) ? 1U : 0U,
                                        (uint8_t)function,
                                        address,
                                        data,
                                        data_size,
                                        block_size);
        if (status == HAL_OK)
        {
            if (sdio_transfer_count < 24U)
            {
                char message[144];

                (void)snprintf(message,
                               sizeof(message),
                               "SDIO[%lu] %c fn=%ld addr=%05lX size=%u block=%u OK\r\n",
                               (unsigned long)sdio_transfer_count,
                               (direction != 0) ? 'W' : 'R',
                               (long)function,
                               (unsigned long)address,
                               (unsigned int)data_size,
                               (unsigned int)block_size);
                bsp_uart_write_string(BSP_UART_DEBUG, message);
            }
            sdio_transfer_count++;
            return WWD_SUCCESS;
        }
    }

    {
        char message[160];

        (void)snprintf(message,
                       sizeof(message),
                       "SDIO FAIL %c fn=%ld addr=%05lX size=%u block=%u hal=%u sta=%08lX\r\n",
                       (direction != 0) ? 'W' : 'R',
                       (long)function,
                       (unsigned long)address,
                       (unsigned int)data_size,
                       (unsigned int)block_size,
                       (unsigned int)status,
                       (unsigned long)READ_REG(SDMMC2->STA));
        bsp_uart_write_string(BSP_UART_DEBUG, message);
    }

    return WWD_SDIO_RETRIES_EXCEEDED;
}

void host_platform_enable_high_speed_sdio(void)
{
    HAL_StatusTypeDef status = bsp_sdio_wifi_set_high_speed();
    char message[96];

    (void)snprintf(message,
                   sizeof(message),
                   "SDIO high speed: hal=%u clock=%lu\r\n",
                   (unsigned int)status,
                   (unsigned long)bsp_sdio_wifi_get_clock());
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}

void host_platform_block_size(int32_t function, int32_t block_size)
{
    uint8_t value;
    uint32_t address;

    if ((function <= 0) || (function > 2) ||
        (block_size <= 0) || (block_size > 512))
    {
        return;
    }

    address = (uint32_t)function * 0x100U + 0x10U;
    value = (uint8_t)block_size;
    if (bsp_sdio_wifi_transfer(1U,
                               0U,
                               address,
                               &value,
                               1U,
                               64U) != HAL_OK)
    {
        return;
    }

    value = (uint8_t)((uint32_t)block_size >> 8);
    if (bsp_sdio_wifi_transfer(1U,
                               0U,
                               address + 1U,
                               &value,
                               1U,
                               64U) == HAL_OK)
    {
        function_block_sizes[function] = (uint16_t)block_size;
        {
            char message[80];

            (void)snprintf(message,
                           sizeof(message),
                           "SDIO fn=%ld block_size=%ld\r\n",
                           (long)function,
                           (long)block_size);
            bsp_uart_write_string(BSP_UART_DEBUG, message);
        }
    }
}

void host_platform_set_card(void *sdio_card)
{
    (void)sdio_card;
}

wwd_result_t host_enable_oob_interrupt(void)
{
    bsp_uart_write_string(BSP_UART_DEBUG, "SDIO OOB IRQ: enable PE3\r\n");
    bsp_sdio_wifi_set_oob_callback(wwd_thread_notify_irq);
    bsp_sdio_wifi_enable_oob_interrupt(1U);
    return WWD_SUCCESS;
}

uint8_t host_platform_get_oob_interrupt_pin(void)
{
    return 0U;
}

wwd_result_t host_platform_bus_enable_interrupt(void)
{
    return WWD_SUCCESS;
}

wwd_result_t host_platform_bus_disable_interrupt(void)
{
    return WWD_SUCCESS;
}

wwd_result_t host_platform_unmask_sdio_interrupt(void)
{
    return WWD_SUCCESS;
}

void host_platform_bus_buffer_freed(int32_t direction)
{
    (void)direction;
}

wwd_result_t host_platform_init(void)
{
    return WWD_SUCCESS;
}

wwd_result_t host_platform_deinit(void)
{
    bsp_sdio_wifi_set_power(0U);
    return WWD_SUCCESS;
}

void host_platform_reset_wifi(int32_t reset_asserted)
{
    bsp_sdio_wifi_set_power((reset_asserted != 0) ? 0U : 1U);
}

void host_platform_power_wifi(int32_t power_enabled)
{
    bsp_sdio_wifi_set_power((power_enabled != 0) ? 1U : 0U);
}

void host_platform_clocks_needed(void)
{
}

void host_platform_clocks_not_needed(void)
{
}

uint32_t host_platform_get_cycle_count(void)
{
    return bsp_dwt_get_cycles();
}

int32_t host_platform_is_in_interrupt_context(void)
{
    return (__get_IPSR() != 0U) ? WICED_TRUE : WICED_FALSE;
}

wwd_result_t host_platform_init_wlan_powersave_clock(void)
{
    return WWD_SUCCESS;
}

wwd_result_t host_platform_deinit_wlan_powersave_clock(void)
{
    return WWD_SUCCESS;
}

void wiced_platform_keep_awake(void)
{
}

void wiced_platform_let_sleep(void)
{
}

int wiced_platform_resource_size(int resource)
{
    if (resource == 0)
    {
        return (int)AP6212_WIFI_IMAGE_SIZE;
    }
    if (resource == 1)
    {
        return (int)sizeof(wifi_nvram_image);
    }
    return 0;
}

int wiced_platform_resource_read(int resource,
                                 uint32_t offset,
                                 void *buffer,
                                 uint32_t buffer_size)
{
    uint32_t resource_size = (uint32_t)wiced_platform_resource_size(resource);

    if ((buffer == NULL) || (buffer_size == 0U) ||
        (resource_size == 0U) || (offset >= resource_size))
    {
        return 0;
    }

    if (resource == 0)
    {
        HAL_StatusTypeDef status;

        status = bsp_w25q128_read(BSP_FLASH_WIFI_IMAGE_ADDRESS + offset,
                                  buffer,
                                  buffer_size);
        if ((resource_read_count < 8U) ||
            ((offset & 0xFFFFU) == 0U) ||
            (((uint64_t)offset + buffer_size) >= resource_size) ||
            (status != HAL_OK))
        {
            char message[128];

            (void)snprintf(message,
                           sizeof(message),
                           "WICED resource[%lu]: off=%lu size=%lu hal=%u\r\n",
                           (unsigned long)resource_read_count,
                           (unsigned long)offset,
                           (unsigned long)buffer_size,
                           (unsigned int)status);
            bsp_uart_write_string(BSP_UART_DEBUG, message);
        }
        resource_read_count++;
        return (status == HAL_OK) ? (int)buffer_size : 0;
    }
    if (resource == 1)
    {
        uint32_t valid_size = resource_size - offset;

        if (valid_size > buffer_size)
        {
            valid_size = buffer_size;
        }
        memset(buffer, 0, buffer_size);
        memcpy(buffer, &wifi_nvram_image[offset], valid_size);
        return (int)buffer_size;
    }
    return 0;
}

int rt_kprintf(const char *format, ...)
{
    char message[192];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    return length;
}

int fputc(int character, FILE *stream)
{
    uint8_t byte = (uint8_t)character;

    (void)stream;
    (void)bsp_uart_write(BSP_UART_DEBUG, &byte, 1U);
    return character;
}
