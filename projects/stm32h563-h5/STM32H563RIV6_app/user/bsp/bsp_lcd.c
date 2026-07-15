/**
 * @file bsp_lcd.c
 * @brief ST7796 display command, drawing, and DMA flush implementation.
 */

#include "bsp_lcd.h"

#include "bsp_gpio.h"
#include "bsp_config.h"
#include "bsp_pwm.h"
#include "bsp_spi.h"

#define BSP_LCD_SPI_TIMEOUT_MS     100U
#define BSP_LCD_FILL_CHUNK_PIXELS  512U

#define ST7796_CMD_SWRESET         0x01U
#define ST7796_CMD_SLPOUT          0x11U
#define ST7796_CMD_INVON           0x21U
#define ST7796_CMD_CASET           0x2AU
#define ST7796_CMD_RASET           0x2BU
#define ST7796_CMD_RAMWR           0x2CU
#define ST7796_CMD_MADCTL          0x36U
#define ST7796_CMD_COLMOD          0x3AU
#define ST7796_CMD_DISPON          0x29U

#define ST7796_COLMOD_RGB565       0x55U
#define ST7796_MADCTL_BGR           0x08U
#define ST7796_MADCTL_LANDSCAPE_BGR (0x20U | ST7796_MADCTL_BGR)

static uint8_t s_lcd_initialized;
static uint8_t s_lcd_tx_chunk[BSP_LCD_FILL_CHUNK_PIXELS * 2U] __attribute__((aligned(32)));
static const uint16_t *s_lcd_dma_pixels;
static uint32_t s_lcd_dma_pixels_left;
static uint8_t s_lcd_dma_active;
static bsp_lcd_flush_complete_cb_t s_lcd_flush_complete_cb;
static void *s_lcd_flush_complete_arg;
static bsp_lcd_health_t s_lcd_health;

/** @brief Start the next bounded display DMA chunk. */
static int lcd_start_next_dma_chunk(void);
/** @brief Receive one logical SPI DMA completion in ISR context. */
static void lcd_spi_tx_complete(bsp_spi_role_t role,
                                bsp_status_t status,
                                void *argument);

/** @brief Assert the display chip select. */
static void lcd_select(void)
{
    (void)bsp_spi_select(BOARD_SPI_DISPLAY, 1U);
}

/** @brief Release the display chip select. */
static void lcd_unselect(void)
{
    (void)bsp_spi_select(BOARD_SPI_DISPLAY, 0U);
}

/** @brief Select command mode on the display D/C signal. */
static void lcd_command_mode(void)
{
    (void)bsp_gpio_write(BOARD_GPIO_LCD_DC, false);
}

/** @brief Select data mode on the display D/C signal. */
static void lcd_data_mode(void)
{
    (void)bsp_gpio_write(BOARD_GPIO_LCD_DC, true);
}

/** @brief Transmit one bounded blocking display fragment. */
static int lcd_tx(const uint8_t *data, uint16_t length)
{
    if((data == NULL) || (length == 0U))
        return 0;

    return bsp_spi_write(BOARD_SPI_DISPLAY,
                         data,
                         length,
                         BSP_LCD_SPI_TIMEOUT_MS) == BSP_STATUS_OK ? 0 : -1;
}

/** @brief Close the active asynchronous flush and notify its owner. */
static void lcd_complete_async_flush(void)
{
    bsp_lcd_flush_complete_cb_t cb = s_lcd_flush_complete_cb;
    void *arg = s_lcd_flush_complete_arg;

    s_lcd_dma_active = 0U;
    s_lcd_dma_pixels = NULL;
    s_lcd_dma_pixels_left = 0U;
    lcd_unselect();

    if(cb != NULL)
    {
        cb(arg);
    }
}

/** @brief Continue an asynchronous flush after one DMA chunk. */
static void lcd_spi_tx_complete(bsp_spi_role_t role,
                                bsp_status_t status,
                                void *argument)
{
    (void)argument;
    if((role != BOARD_SPI_DISPLAY) || (s_lcd_dma_active == 0U))
    {
        return;
    }
    if(status != BSP_STATUS_OK)
    {
        s_lcd_health.dma_transfer_errors++;
        lcd_complete_async_flush();
        return;
    }
    (void)lcd_start_next_dma_chunk();
}

/** @brief Convert and start the next bounded RGB565 DMA fragment. */
static int lcd_start_next_dma_chunk(void)
{
    uint16_t pixels_now;
    uint16_t i;
    uint16_t bytes_now;

    if(s_lcd_dma_pixels_left == 0U)
    {
        s_lcd_health.dma_flushes_completed++;
        lcd_complete_async_flush();
        return 0;
    }

    pixels_now = (s_lcd_dma_pixels_left > BSP_LCD_FILL_CHUNK_PIXELS) ?
                 BSP_LCD_FILL_CHUNK_PIXELS : (uint16_t)s_lcd_dma_pixels_left;

    for(i = 0U; i < pixels_now; i++)
    {
        uint16_t color = *s_lcd_dma_pixels++;
        s_lcd_tx_chunk[(i * 2U)] = (uint8_t)(color >> 8);
        s_lcd_tx_chunk[(i * 2U) + 1U] = (uint8_t)(color & 0xFFU);
    }
    s_lcd_dma_pixels_left -= pixels_now;
    bytes_now = (uint16_t)(pixels_now * 2U);

    if(bsp_spi_write_dma(BOARD_SPI_DISPLAY,
                         s_lcd_tx_chunk,
                         bytes_now,
                         lcd_spi_tx_complete,
                         NULL) != BSP_STATUS_OK)
    {
        s_lcd_health.dma_start_errors++;
        lcd_complete_async_flush();
        return -1;
    }

    s_lcd_health.dma_bytes += bytes_now;

    return 0;
}

/** @brief Write one display command. */
static int lcd_write_command(uint8_t command)
{
    int ret;

    lcd_select();
    lcd_command_mode();
    ret = lcd_tx(&command, 1U);
    lcd_unselect();

    return ret;
}

/** @brief Write one display data fragment. */
static int lcd_write_data(const uint8_t *data, uint16_t length)
{
    int ret;

    lcd_select();
    lcd_data_mode();
    ret = lcd_tx(data, length);
    lcd_unselect();

    return ret;
}

/** @brief Write one command followed by its data payload. */
static int lcd_write_command_data(uint8_t command, const uint8_t *data, uint16_t length)
{
    if(lcd_write_command(command) != 0)
        return -1;

    return lcd_write_data(data, length);
}

/** @brief Set LCD backlight PWM to full duty. */
void bsp_lcd_backlight_on(void)
{
    const bsp_pwm_config_t config =
    {
        .frequency_hz = BOARD_PWM_LCD_DEFAULT_FREQUENCY_HZ,
        .duty_permille = 1000U
    };
    (void)bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT, &config, NULL);
    (void)bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT);
}

/** @brief Set LCD backlight PWM to zero duty. */
void bsp_lcd_backlight_off(void)
{
    const bsp_pwm_config_t config =
    {
        .frequency_hz = BOARD_PWM_LCD_DEFAULT_FREQUENCY_HZ,
        .duty_permille = 0U
    };
    (void)bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT, &config, NULL);
    (void)bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT);
}

/** @brief Perform the active-low display reset sequence. */
void bsp_lcd_reset(void)
{
    (void)bsp_gpio_write(BOARD_GPIO_LCD_RESET, true);
    HAL_Delay(20U);
    (void)bsp_gpio_write(BOARD_GPIO_LCD_RESET, false);
    HAL_Delay(120U);
}

/** @brief Program the active LCD address window. */
int bsp_lcd_set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t x2;
    uint16_t y2;
    uint8_t data[4];

    if((width == 0U) || (height == 0U))
        return -1;

    if((x >= BSP_LCD_WIDTH) || (y >= BSP_LCD_HEIGHT))
        return -1;

    if(((uint32_t)x + width) > BSP_LCD_WIDTH)
        return -1;

    if(((uint32_t)y + height) > BSP_LCD_HEIGHT)
        return -1;

    x2 = (uint16_t)(x + width - 1U);
    y2 = (uint16_t)(y + height - 1U);

    data[0] = (uint8_t)(x >> 8);
    data[1] = (uint8_t)(x & 0xFFU);
    data[2] = (uint8_t)(x2 >> 8);
    data[3] = (uint8_t)(x2 & 0xFFU);
    if(lcd_write_command_data(ST7796_CMD_CASET, data, sizeof(data)) != 0)
        return -1;

    data[0] = (uint8_t)(y >> 8);
    data[1] = (uint8_t)(y & 0xFFU);
    data[2] = (uint8_t)(y2 >> 8);
    data[3] = (uint8_t)(y2 & 0xFFU);
    return lcd_write_command_data(ST7796_CMD_RASET, data, sizeof(data));
}

/** @brief Fill the complete panel with one RGB565 color. */
int bsp_lcd_fill_color(uint16_t rgb565)
{
    uint32_t pixels_left = BSP_LCD_WIDTH * BSP_LCD_HEIGHT;
    uint32_t i;
    uint8_t ramwr = ST7796_CMD_RAMWR;

    if(bsp_lcd_set_window(0U, 0U, BSP_LCD_WIDTH, BSP_LCD_HEIGHT) != 0)
        return -1;

    for(i = 0U; i < BSP_LCD_FILL_CHUNK_PIXELS; i++)
    {
        s_lcd_tx_chunk[(i * 2U)] = (uint8_t)(rgb565 >> 8);
        s_lcd_tx_chunk[(i * 2U) + 1U] = (uint8_t)(rgb565 & 0xFFU);
    }

    /* Send RAMWR + pixel data with CS held low continuously. */
    lcd_select();
    lcd_command_mode();
    if(lcd_tx(&ramwr, 1U) != 0)
    {
        lcd_unselect();
        return -1;
    }
    lcd_data_mode();
    while(pixels_left > 0U)
    {
        uint16_t pixels_now = (pixels_left > BSP_LCD_FILL_CHUNK_PIXELS) ?
                              BSP_LCD_FILL_CHUNK_PIXELS : (uint16_t)pixels_left;

        if(lcd_tx(s_lcd_tx_chunk, (uint16_t)(pixels_now * 2U)) != 0)
        {
            lcd_unselect();
            return -1;
        }

        pixels_left -= pixels_now;
    }
    lcd_unselect();

    return 0;
}

/** @brief Draw one RGB565 rectangle with blocking SPI transfers. */
int bsp_lcd_draw_rgb565(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        const uint16_t *pixels)
{
    uint32_t pixels_left;
    uint8_t ramwr = ST7796_CMD_RAMWR;

    if(pixels == NULL)
        return -1;

    if(bsp_lcd_set_window(x, y, width, height) != 0)
        return -1;

    pixels_left = (uint32_t)width * height;

    /* Send RAMWR + pixel data with CS held low continuously. */
    lcd_select();
    lcd_command_mode();
    if(lcd_tx(&ramwr, 1U) != 0)
    {
        lcd_unselect();
        return -1;
    }
    lcd_data_mode();
    while(pixels_left > 0U)
    {
        uint16_t pixels_now = (pixels_left > BSP_LCD_FILL_CHUNK_PIXELS) ?
                              BSP_LCD_FILL_CHUNK_PIXELS : (uint16_t)pixels_left;
        uint16_t i;

        for(i = 0U; i < pixels_now; i++)
        {
            uint16_t color = *pixels++;
            s_lcd_tx_chunk[(i * 2U)] = (uint8_t)(color >> 8);
            s_lcd_tx_chunk[(i * 2U) + 1U] = (uint8_t)(color & 0xFFU);
        }

        if(lcd_tx(s_lcd_tx_chunk, (uint16_t)(pixels_now * 2U)) != 0)
        {
            lcd_unselect();
            return -1;
        }

        pixels_left -= pixels_now;
    }
    lcd_unselect();

    return 0;
}

/** @brief Register the callback notified after an asynchronous DMA flush. */
void bsp_lcd_set_flush_complete_callback(bsp_lcd_flush_complete_cb_t cb, void *arg)
{
    s_lcd_flush_complete_cb = cb;
    s_lcd_flush_complete_arg = arg;
}

/** @brief Start an asynchronous RGB565 rectangle transfer. */
int bsp_lcd_draw_rgb565_dma(uint16_t x,
                            uint16_t y,
                            uint16_t width,
                            uint16_t height,
                            const uint16_t *pixels)
{
    uint8_t ramwr = ST7796_CMD_RAMWR;

    if(pixels == NULL)
        return -1;

    if(s_lcd_dma_active != 0U)
    {
        s_lcd_health.dma_busy_rejects++;
        return -1;
    }

    if(bsp_lcd_set_window(x, y, width, height) != 0)
        return -1;

    s_lcd_dma_pixels = pixels;
    s_lcd_dma_pixels_left = (uint32_t)width * height;
    s_lcd_dma_active = 1U;
    s_lcd_health.dma_flushes_started++;

    lcd_select();
    lcd_command_mode();
    if(lcd_tx(&ramwr, 1U) != 0)
    {
        lcd_complete_async_flush();
        return -1;
    }

    lcd_data_mode();
    if(lcd_start_next_dma_chunk() != 0)
    {
        return -1;
    }

    return 0;
}

/** @brief Copy LCD DMA health without exposing its mutable ownership state. */
int bsp_lcd_get_health(bsp_lcd_health_t *health)
{
    if(health == NULL)
    {
        return -1;
    }

    *health = s_lcd_health;
    return 0;
}

/** @brief Initialize the ST7796 panel and enable its backlight. */
int bsp_lcd_init(void)
{
    uint8_t value;

    if(s_lcd_initialized)
        return 0;

    bsp_lcd_backlight_off();
    bsp_lcd_reset();

    if(lcd_write_command(ST7796_CMD_SWRESET) != 0)
        return -1;
    HAL_Delay(120U);

    if(lcd_write_command(ST7796_CMD_SLPOUT) != 0)
        return -1;
    HAL_Delay(120U);

    value = ST7796_MADCTL_LANDSCAPE_BGR;
    if(lcd_write_command_data(ST7796_CMD_MADCTL, &value, 1U) != 0)
        return -1;

    value = ST7796_COLMOD_RGB565;
    if(lcd_write_command_data(ST7796_CMD_COLMOD, &value, 1U) != 0)
        return -1;

    if(lcd_write_command(ST7796_CMD_INVON) != 0)
        return -1;

    if(lcd_write_command(ST7796_CMD_DISPON) != 0)
        return -1;
    HAL_Delay(20U);

    if(bsp_lcd_fill_color(BSP_LCD_COLOR_BLACK) != 0)
        return -1;

    bsp_lcd_backlight_on();
    s_lcd_initialized = 1U;

    return 0;
}
