/**
 * @file bsp_gpio.c
 * @brief Safe-state GPIO, polarity, and logical control implementation.
 */

#include "bsp_gpio.h"

#include <stddef.h>

#include "bsp_config.h"

/** @brief Electrical descriptor for one logical GPIO role. */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t pull;
    bool active_low;
    bool is_output;
    bool safe_active;
} bsp_gpio_descriptor_t;

static const bsp_gpio_descriptor_t g_bsp_gpio_descriptors[BOARD_GPIO_COUNT] =
{
    [BOARD_GPIO_STATUS_LED] =
        {BOARD_STATUS_LED_PORT, BOARD_STATUS_LED_PIN, GPIO_NOPULL, true, true, false},
    [BOARD_GPIO_W800_BOOT] =
        {BOARD_W800_BOOT_PORT, BOARD_W800_BOOT_PIN, GPIO_NOPULL, false, true, true},
    [BOARD_GPIO_W800_RESET] =
        {BOARD_W800_RESET_PORT, BOARD_W800_RESET_PIN, GPIO_NOPULL, true, true, false},
    [BOARD_GPIO_W800_WAKE] =
        {BOARD_W800_WAKE_PORT, BOARD_W800_WAKE_PIN, GPIO_NOPULL, false, true, false},
    [BOARD_GPIO_FLASH_CS] =
        {BOARD_SPI_FLASH_CS_PORT, BOARD_SPI_FLASH_CS_PIN, GPIO_NOPULL, true, true, false},
    [BOARD_GPIO_LCD_CS] =
        {BOARD_SPI_LCD_CS_PORT, BOARD_SPI_LCD_CS_PIN, GPIO_NOPULL, true, true, false},
    [BOARD_GPIO_LCD_DC] =
        {BOARD_LCD_DC_PORT, BOARD_LCD_DC_PIN, GPIO_NOPULL, false, true, true},
    [BOARD_GPIO_LCD_RESET] =
        {BOARD_LCD_RESET_PORT, BOARD_LCD_RESET_PIN, GPIO_NOPULL, true, true, false},
    [BOARD_GPIO_LCD_BACKLIGHT_SAFE] =
        {BOARD_PWM_LCD_PORT, BOARD_PWM_LCD_PIN, GPIO_NOPULL, false, true, false},
    [BOARD_GPIO_TOUCH_RESET] =
        {BOARD_TOUCH_RESET_PORT, BOARD_TOUCH_RESET_PIN, GPIO_NOPULL, true, true, true},
    [BOARD_GPIO_TOUCH_INTERRUPT] =
        {BOARD_TOUCH_INTERRUPT_PORT, BOARD_TOUCH_INTERRUPT_PIN, GPIO_NOPULL, true, false, false},
    [BOARD_GPIO_USB_ID] =
        {BOARD_USB_ID_PORT, BOARD_USB_ID_PIN, GPIO_NOPULL, false, false, false}
};

static bool g_bsp_gpio_initialized;

/** @brief Convert a logical state to the configured electrical level. */
static GPIO_PinState bsp_gpio_to_level(const bsp_gpio_descriptor_t *descriptor,
                                         bool is_active)
{
    bool is_high = descriptor->active_low ? !is_active : is_active;
    return is_high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/** @brief Convert an electrical level to the configured logical state. */
static bool bsp_gpio_from_level(const bsp_gpio_descriptor_t *descriptor,
                                  GPIO_PinState level)
{
    bool is_high = level == GPIO_PIN_SET;
    return descriptor->active_low ? !is_high : is_high;
}

/** @brief Implement bsp_gpio_init() and apply every output safe state first. */
bsp_status_t bsp_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t index;

    if(g_bsp_gpio_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    for(index = 0U; index < BOARD_GPIO_COUNT; index++)
    {
        const bsp_gpio_descriptor_t *descriptor = &g_bsp_gpio_descriptors[index];

        gpio.Pin = descriptor->pin;
        gpio.Mode = descriptor->is_output ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_INPUT;
        gpio.Pull = descriptor->pull;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        if(descriptor->is_output)
        {
            HAL_GPIO_WritePin(descriptor->port,
                              descriptor->pin,
                              bsp_gpio_to_level(descriptor,
                                                  descriptor->safe_active));
        }
        HAL_GPIO_Init(descriptor->port, &gpio);
    }

    g_bsp_gpio_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Implement bsp_gpio_write() for board-owned outputs. */
bsp_status_t bsp_gpio_write(bsp_gpio_role_t role, bool is_active)
{
    const bsp_gpio_descriptor_t *descriptor;

    if(role >= BOARD_GPIO_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_bsp_gpio_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    descriptor = &g_bsp_gpio_descriptors[role];
    if(!descriptor->is_output)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    HAL_GPIO_WritePin(descriptor->port,
                      descriptor->pin,
                      bsp_gpio_to_level(descriptor, is_active));
    return BSP_STATUS_OK;
}

/** @brief Implement bsp_gpio_read() for input and output roles. */
bsp_status_t bsp_gpio_read(bsp_gpio_role_t role, bool *is_active)
{
    const bsp_gpio_descriptor_t *descriptor;

    if((role >= BOARD_GPIO_COUNT) || (is_active == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_bsp_gpio_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    descriptor = &g_bsp_gpio_descriptors[role];
    *is_active = bsp_gpio_from_level(descriptor,
                                       HAL_GPIO_ReadPin(descriptor->port,
                                                        descriptor->pin));
    return BSP_STATUS_OK;
}

/** @brief Implement bsp_gpio_toggle() while retaining logical polarity. */
bsp_status_t bsp_gpio_toggle(bsp_gpio_role_t role)
{
    bool is_active;
    bsp_status_t status = bsp_gpio_read(role, &is_active);

    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    if(!g_bsp_gpio_descriptors[role].is_output)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }
    return bsp_gpio_write(role, !is_active);
}
