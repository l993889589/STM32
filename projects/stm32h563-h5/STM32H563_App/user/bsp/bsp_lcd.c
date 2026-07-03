#include "bsp_lcd.h"

#include "spi.h"

#define BSP_LCD_SPI_TIMEOUT_MS     100U
#define BSP_LCD_FILL_CHUNK_PIXELS  64U

#define ST7796_CMD_SWRESET         0x01U
#define ST7796_CMD_SLPOUT          0x11U
#define ST7796_CMD_CASET           0x2AU
#define ST7796_CMD_RASET           0x2BU
#define ST7796_CMD_RAMWR           0x2CU
#define ST7796_CMD_MADCTL          0x36U
#define ST7796_CMD_COLMOD          0x3AU
#define ST7796_CMD_DISPON          0x29U

#define ST7796_COLMOD_RGB565       0x55U
#define ST7796_MADCTL_LANDSCAPE_RGB 0x20U

static uint8_t s_lcd_initialized;

static void lcd_select(void)
{
    HAL_GPIO_WritePin(BSP_LCD_CS_PORT, BSP_LCD_CS_PIN, GPIO_PIN_RESET);
}

static void lcd_unselect(void)
{
    HAL_GPIO_WritePin(BSP_LCD_CS_PORT, BSP_LCD_CS_PIN, GPIO_PIN_SET);
}

static void lcd_command_mode(void)
{
    HAL_GPIO_WritePin(BSP_LCD_DC_PORT, BSP_LCD_DC_PIN, GPIO_PIN_RESET);
}

static void lcd_data_mode(void)
{
    HAL_GPIO_WritePin(BSP_LCD_DC_PORT, BSP_LCD_DC_PIN, GPIO_PIN_SET);
}

static int lcd_tx(const uint8_t *data, uint16_t length)
{
    if((data == NULL) || (length == 0U))
        return 0;

    return (HAL_SPI_Transmit(&hspi2, (uint8_t *)data, length, BSP_LCD_SPI_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int lcd_write_command(uint8_t command)
{
    int ret;

    lcd_select();
    lcd_command_mode();
    ret = lcd_tx(&command, 1U);
    lcd_unselect();

    return ret;
}

static int lcd_write_data(const uint8_t *data, uint16_t length)
{
    int ret;

    lcd_select();
    lcd_data_mode();
    ret = lcd_tx(data, length);
    lcd_unselect();

    return ret;
}

static int lcd_write_command_data(uint8_t command, const uint8_t *data, uint16_t length)
{
    if(lcd_write_command(command) != 0)
        return -1;

    return lcd_write_data(data, length);
}

static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(BSP_LCD_RESET_PORT, BSP_LCD_RESET_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BSP_LCD_BACKLIGHT_PORT, BSP_LCD_BACKLIGHT_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BSP_LCD_TOUCH_RESET_PORT, BSP_LCD_TOUCH_RESET_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BSP_LCD_CS_PORT, BSP_LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BSP_LCD_DC_PORT, BSP_LCD_DC_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = BSP_LCD_RESET_PIN | BSP_LCD_BACKLIGHT_PIN | BSP_LCD_TOUCH_RESET_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BSP_LCD_TOUCH_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BSP_LCD_TOUCH_INT_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BSP_LCD_CS_PIN | BSP_LCD_DC_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void bsp_lcd_backlight_on(void)
{
    HAL_GPIO_WritePin(BSP_LCD_BACKLIGHT_PORT, BSP_LCD_BACKLIGHT_PIN, GPIO_PIN_SET);
}

void bsp_lcd_backlight_off(void)
{
    HAL_GPIO_WritePin(BSP_LCD_BACKLIGHT_PORT, BSP_LCD_BACKLIGHT_PIN, GPIO_PIN_RESET);
}

void bsp_lcd_reset(void)
{
    HAL_GPIO_WritePin(BSP_LCD_RESET_PORT, BSP_LCD_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(BSP_LCD_RESET_PORT, BSP_LCD_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(120U);
}

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

int bsp_lcd_fill_color(uint16_t rgb565)
{
    uint8_t chunk[BSP_LCD_FILL_CHUNK_PIXELS * 2U];
    uint32_t pixels_left = BSP_LCD_WIDTH * BSP_LCD_HEIGHT;
    uint32_t i;
    uint8_t ramwr = ST7796_CMD_RAMWR;

    if(bsp_lcd_set_window(0U, 0U, BSP_LCD_WIDTH, BSP_LCD_HEIGHT) != 0)
        return -1;

    for(i = 0U; i < BSP_LCD_FILL_CHUNK_PIXELS; i++)
    {
        chunk[(i * 2U)] = (uint8_t)(rgb565 >> 8);
        chunk[(i * 2U) + 1U] = (uint8_t)(rgb565 & 0xFFU);
    }

    /* Send RAMWR + pixel data with CS held low continuously. */
    lcd_select();
    lcd_command_mode();
    lcd_tx(&ramwr, 1U);
    lcd_data_mode();
    while(pixels_left > 0U)
    {
        uint16_t pixels_now = (pixels_left > BSP_LCD_FILL_CHUNK_PIXELS) ?
                              BSP_LCD_FILL_CHUNK_PIXELS : (uint16_t)pixels_left;

        if(lcd_tx(chunk, (uint16_t)(pixels_now * 2U)) != 0)
        {
            lcd_unselect();
            return -1;
        }

        pixels_left -= pixels_now;
    }
    lcd_unselect();

    return 0;
}

int bsp_lcd_draw_rgb565(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        const uint16_t *pixels)
{
    uint8_t chunk[BSP_LCD_FILL_CHUNK_PIXELS * 2U];
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
    lcd_tx(&ramwr, 1U);
    lcd_data_mode();
    while(pixels_left > 0U)
    {
        uint16_t pixels_now = (pixels_left > BSP_LCD_FILL_CHUNK_PIXELS) ?
                              BSP_LCD_FILL_CHUNK_PIXELS : (uint16_t)pixels_left;
        uint16_t i;

        for(i = 0U; i < pixels_now; i++)
        {
            uint16_t color = *pixels++;
            chunk[(i * 2U)] = (uint8_t)(color >> 8);
            chunk[(i * 2U) + 1U] = (uint8_t)(color & 0xFFU);
        }

        if(lcd_tx(chunk, (uint16_t)(pixels_now * 2U)) != 0)
        {
            lcd_unselect();
            return -1;
        }

        pixels_left -= pixels_now;
    }
    lcd_unselect();

    return 0;
}

int bsp_lcd_init(void)
{
    uint8_t value;

    if(s_lcd_initialized)
        return 0;

    lcd_gpio_init();
    if(hspi2.Instance != SPI2)
        MX_SPI2_Init();

    bsp_lcd_backlight_off();
    bsp_lcd_reset();

    if(lcd_write_command(ST7796_CMD_SWRESET) != 0)
        return -1;
    HAL_Delay(120U);

    if(lcd_write_command(ST7796_CMD_SLPOUT) != 0)
        return -1;
    HAL_Delay(120U);

    value = ST7796_MADCTL_LANDSCAPE_RGB;
    if(lcd_write_command_data(ST7796_CMD_MADCTL, &value, 1U) != 0)
        return -1;

    value = ST7796_COLMOD_RGB565;
    if(lcd_write_command_data(ST7796_CMD_COLMOD, &value, 1U) != 0)
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
