/**
 * @file st7796.c
 * @brief ST7796 display implementation over SPI2 and board control GPIOs.
 */

#include "st7796.h"

#include <stdbool.h>
#include <stddef.h>

#include "board_control.h"
#include "bsp_pwm.h"
#include "bsp_spi.h"
#include "bsp_time.h"

#define ST7796_CMD_SWRESET (0x01U)
#define ST7796_CMD_SLPOUT  (0x11U)
#define ST7796_CMD_INVON   (0x21U)
#define ST7796_CMD_DISPON  (0x29U)
#define ST7796_CMD_CASET   (0x2AU)
#define ST7796_CMD_RASET   (0x2BU)
#define ST7796_CMD_RAMWR   (0x2CU)
#define ST7796_CMD_MADCTL  (0x36U)
#define ST7796_CMD_COLMOD  (0x3AU)
#define ST7796_MADCTL_LANDSCAPE_BGR (0x28U)
#define ST7796_COLMOD_RGB565        (0x55U)
#define ST7796_SPI_TIMEOUT_MS       (20U)
#define ST7796_TRANSFER_BYTES       (256U)

static uint8_t st7796_transfer_buffer[ST7796_TRANSFER_BYTES];
static bool st7796_is_initialized;

/**
 * @brief Send one command and optional data while owning display chip select.
 */
static bsp_status_t st7796_write_command_data(uint8_t command,
                                                  const uint8_t *data,
                                                  uint32_t length)
{
    bsp_status_t status = bsp_spi_select(BOARD_SPI_DISPLAY, 1U);

    if(status == BSP_STATUS_OK)
    {
        status = board_control_write(BOARD_CONTROL_DISPLAY_DC, false);
    }
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_write(BOARD_SPI_DISPLAY,
                               &command,
                               1U,
                               ST7796_SPI_TIMEOUT_MS);
    }
    if((status == BSP_STATUS_OK) && (length > 0U))
    {
        status = board_control_write(BOARD_CONTROL_DISPLAY_DC, true);
    }
    if((status == BSP_STATUS_OK) && (length > 0U))
    {
        status = bsp_spi_write(BOARD_SPI_DISPLAY,
                               data,
                               length,
                               ST7796_SPI_TIMEOUT_MS);
    }
    (void)bsp_spi_select(BOARD_SPI_DISPLAY, 0U);
    return status;
}

/**
 * @brief Begin a display RAM write and leave chip select asserted for pixel data.
 */
static bsp_status_t st7796_begin_memory_write(void)
{
    const uint8_t command = ST7796_CMD_RAMWR;
    bsp_status_t status = bsp_spi_select(BOARD_SPI_DISPLAY, 1U);

    if(status == BSP_STATUS_OK)
    {
        status = board_control_write(BOARD_CONTROL_DISPLAY_DC, false);
    }
    if(status == BSP_STATUS_OK)
    {
        status = bsp_spi_write(BOARD_SPI_DISPLAY,
                               &command,
                               1U,
                               ST7796_SPI_TIMEOUT_MS);
    }
    if(status == BSP_STATUS_OK)
    {
        status = board_control_write(BOARD_CONTROL_DISPLAY_DC, true);
    }
    if(status != BSP_STATUS_OK)
    {
        (void)bsp_spi_select(BOARD_SPI_DISPLAY, 0U);
    }
    return status;
}

/**
 * @brief Implement st7796_init() as documented by its interface contract.
 */
bsp_status_t st7796_init(void)
{
    const bsp_spi_config_t spi_config =
    {
        .baud_rate_hz = 20000000U,
        .clock_polarity = BSP_SPI_CLOCK_POLARITY_LOW,
        .clock_phase = BSP_SPI_CLOCK_PHASE_FIRST_EDGE
    };
    const bsp_pwm_config_t pwm_config = {20000U, 0U};
    uint8_t value;
    bsp_status_t status;

    if(st7796_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    status = bsp_spi_init(BOARD_SPI_DISPLAY, &spi_config);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = bsp_pwm_init(BOARD_PWM_LCD_BACKLIGHT, &pwm_config, NULL);
    if(status == BSP_STATUS_ALREADY_INITIALIZED)
    {
        status = bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT,
                                   &pwm_config,
                                   NULL);
    }
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = board_control_write(BOARD_CONTROL_DISPLAY_RESET, true);
    if(status == BSP_STATUS_OK)
    {
        bsp_time_delay_ms(20U);
        status = board_control_write(BOARD_CONTROL_DISPLAY_RESET, false);
    }
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    bsp_time_delay_ms(120U);

    status = st7796_write_command_data(ST7796_CMD_SWRESET, NULL, 0U);
    if(status == BSP_STATUS_OK)
    {
        bsp_time_delay_ms(120U);
        status = st7796_write_command_data(ST7796_CMD_SLPOUT,
                                               NULL,
                                               0U);
    }
    if(status == BSP_STATUS_OK)
    {
        bsp_time_delay_ms(120U);
        value = ST7796_MADCTL_LANDSCAPE_BGR;
        status = st7796_write_command_data(ST7796_CMD_MADCTL,
                                               &value,
                                               1U);
    }
    if(status == BSP_STATUS_OK)
    {
        value = ST7796_COLMOD_RGB565;
        status = st7796_write_command_data(ST7796_CMD_COLMOD,
                                               &value,
                                               1U);
    }
    if(status == BSP_STATUS_OK)
    {
        status = st7796_write_command_data(ST7796_CMD_INVON,
                                               NULL,
                                               0U);
    }
    if(status == BSP_STATUS_OK)
    {
        status = st7796_write_command_data(ST7796_CMD_DISPON,
                                               NULL,
                                               0U);
    }
    if(status == BSP_STATUS_OK)
    {
        bsp_time_delay_ms(20U);
        st7796_is_initialized = true;
    }
    return status;
}

/**
 * @brief Implement st7796_set_backlight() as documented by its interface contract.
 */
bsp_status_t st7796_set_backlight(uint16_t duty_permille)
{
    const bsp_pwm_config_t config = {20000U, duty_permille};
    bsp_status_t status;

    if(!st7796_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(duty_permille > 1000U)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT, &config, NULL);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    return duty_permille == 0U ?
           bsp_pwm_stop(BOARD_PWM_LCD_BACKLIGHT) :
           bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT);
}

/**
 * @brief Implement st7796_set_window() as documented by its interface contract.
 */
bsp_status_t st7796_set_window(uint16_t x,
                                   uint16_t y,
                                   uint16_t width,
                                   uint16_t height)
{
    uint32_t x_end;
    uint32_t y_end;
    uint8_t data[4];
    bsp_status_t status;

    if(!st7796_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((width == 0U) || (height == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    x_end = (uint32_t)x + width - 1U;
    y_end = (uint32_t)y + height - 1U;
    if((x_end >= ST7796_WIDTH) || (y_end >= ST7796_HEIGHT))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    data[0] = (uint8_t)(x >> 8);
    data[1] = (uint8_t)x;
    data[2] = (uint8_t)(x_end >> 8);
    data[3] = (uint8_t)x_end;
    status = st7796_write_command_data(ST7796_CMD_CASET,
                                           data,
                                           sizeof(data));
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    data[0] = (uint8_t)(y >> 8);
    data[1] = (uint8_t)y;
    data[2] = (uint8_t)(y_end >> 8);
    data[3] = (uint8_t)y_end;
    return st7796_write_command_data(ST7796_CMD_RASET,
                                         data,
                                         sizeof(data));
}

/**
 * @brief Implement st7796_write_pixels() as documented by its interface contract.
 */
bsp_status_t st7796_write_pixels(const uint16_t *pixels,
                                     uint32_t pixel_count,
                                     uint32_t timeout_ms)
{
    uint32_t index;
    uint32_t chunk_pixels;
    bsp_status_t status;

    if(!st7796_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((pixels == NULL) || (pixel_count == 0U) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = st7796_begin_memory_write();
    while((status == BSP_STATUS_OK) && (pixel_count > 0U))
    {
        chunk_pixels = pixel_count > (sizeof(st7796_transfer_buffer) / 2U) ?
                       (sizeof(st7796_transfer_buffer) / 2U) : pixel_count;
        for(index = 0U; index < chunk_pixels; index++)
        {
            st7796_transfer_buffer[index * 2U] =
                (uint8_t)(pixels[index] >> 8);
            st7796_transfer_buffer[index * 2U + 1U] =
                (uint8_t)pixels[index];
        }
        status = bsp_spi_write(BOARD_SPI_DISPLAY,
                               st7796_transfer_buffer,
                               chunk_pixels * 2U,
                               timeout_ms);
        pixels += chunk_pixels;
        pixel_count -= chunk_pixels;
    }
    (void)bsp_spi_select(BOARD_SPI_DISPLAY, 0U);
    return status;
}

/**
 * @brief Implement st7796_fill() as documented by its interface contract.
 */
bsp_status_t st7796_fill(uint16_t color,
                             uint32_t pixel_count,
                             uint32_t timeout_ms)
{
    uint32_t index;
    uint32_t chunk_pixels;
    bsp_status_t status;

    if(!st7796_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((pixel_count == 0U) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    for(index = 0U; index < (sizeof(st7796_transfer_buffer) / 2U); index++)
    {
        st7796_transfer_buffer[index * 2U] = (uint8_t)(color >> 8);
        st7796_transfer_buffer[index * 2U + 1U] = (uint8_t)color;
    }

    status = st7796_begin_memory_write();
    while((status == BSP_STATUS_OK) && (pixel_count > 0U))
    {
        chunk_pixels = pixel_count > (sizeof(st7796_transfer_buffer) / 2U) ?
                       (sizeof(st7796_transfer_buffer) / 2U) : pixel_count;
        status = bsp_spi_write(BOARD_SPI_DISPLAY,
                               st7796_transfer_buffer,
                               chunk_pixels * 2U,
                               timeout_ms);
        pixel_count -= chunk_pixels;
    }
    (void)bsp_spi_select(BOARD_SPI_DISPLAY, 0U);
    return status;
}
