/**
 * @file board_control.c
 * @brief GPIO and polarity binding for Wi-Fi, display, touch, and USB-ID controls.
 */

#include "board_control.h"

#include "stm32h5xx_hal.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    bool active_low;
    bool is_output;
} board_control_descriptor_t;

static const board_control_descriptor_t board_control_descriptors[BOARD_CONTROL_COUNT] =
{
    [BOARD_CONTROL_WIFI_BOOT]      = {GPIOA, GPIO_PIN_8,  false, true},
    [BOARD_CONTROL_WIFI_RESET]     = {GPIOC, GPIO_PIN_9,  true,  true},
    [BOARD_CONTROL_WIFI_WAKE]      = {GPIOC, GPIO_PIN_8,  false, true},
    [BOARD_CONTROL_DISPLAY_DC]     = {GPIOD, GPIO_PIN_12, false, true},
    [BOARD_CONTROL_DISPLAY_RESET]  = {GPIOB, GPIO_PIN_4,  true,  true},
    [BOARD_CONTROL_TOUCH_RESET]    = {GPIOB, GPIO_PIN_15, true,  true},
    [BOARD_CONTROL_TOUCH_INTERRUPT]= {GPIOB, GPIO_PIN_14, true,  false},
    [BOARD_CONTROL_USB_ID]         = {GPIOB, GPIO_PIN_13, false, false}
};

static bool board_control_is_initialized;

/**
 * @brief Convert a logical active state to the electrical GPIO level.
 */
static GPIO_PinState board_control_to_level(const board_control_descriptor_t *descriptor,
                                            bool is_active)
{
    const bool is_high = descriptor->active_low ? !is_active : is_active;
    return is_high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/**
 * @brief Convert an electrical GPIO level to the logical active state.
 */
static bool board_control_from_level(const board_control_descriptor_t *descriptor,
                                     GPIO_PinState level)
{
    const bool is_high = level == GPIO_PIN_SET;
    return descriptor->active_low ? !is_high : is_high;
}

/**
 * @brief Implement board_control_init() as documented by its interface contract.
 */
bsp_status_t board_control_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if(board_control_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4 | GPIO_PIN_15, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    board_control_is_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement board_control_write() as documented by its interface contract.
 */
bsp_status_t board_control_write(board_control_role_t role, bool is_active)
{
    const board_control_descriptor_t *descriptor;

    if(role >= BOARD_CONTROL_COUNT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_control_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    descriptor = &board_control_descriptors[role];
    if(!descriptor->is_output)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    HAL_GPIO_WritePin(descriptor->port,
                      descriptor->pin,
                      board_control_to_level(descriptor, is_active));
    return BSP_STATUS_OK;
}

/**
 * @brief Implement board_control_read() as documented by its interface contract.
 */
bsp_status_t board_control_read(board_control_role_t role, bool *is_active)
{
    const board_control_descriptor_t *descriptor;

    if((role >= BOARD_CONTROL_COUNT) || (is_active == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_control_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    descriptor = &board_control_descriptors[role];
    *is_active = board_control_from_level(descriptor,
                                          HAL_GPIO_ReadPin(descriptor->port,
                                                           descriptor->pin));
    return BSP_STATUS_OK;
}
