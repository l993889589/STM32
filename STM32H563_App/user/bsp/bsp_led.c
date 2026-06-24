/**
 * @file    bsp_led.c
 * @brief   通用 LED BSP 驱动实现 (基于 STM32 HAL 库)
 *
 * @author  leduo
 * @version 1.0.0
 * @date    2026-06-15
 */

#include "bsp.h"

/* ============================================================
 * 私有变量：根据配置表生成 LED 配置数组（只读）
 * ============================================================ */
static const BSP_LED_Config_t s_ledConfig[LED_COUNT] = BSP_LED_CONFIG_TABLE;

/* ============================================================
 * 私有辅助函数：参数合法性检查
 * ============================================================ */
static inline uint8_t _LED_IsValidIndex(BSP_LED_Index_t led)
{
    return (led < LED_COUNT);
}

/* ============================================================
 * 公共 API 实现
 * ============================================================ */

/**
 * @brief  初始化所有 LED，将 GPIO 设置为输出模式并熄灭所有灯
 * @note   GPIO 时钟必须在调用此函数之前已使能（CubeMX 生成的 MX_GPIO_Init 会完成）
 *         若未使用 CubeMX，请在此处手动调用 __HAL_RCC_GPIOx_CLK_ENABLE()
 */
void bsp_led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    for (uint8_t i = 0U; i < (uint8_t)LED_COUNT; i++)
    {
        /* 先熄灭，防止初始化瞬间闪烁 */
        HAL_GPIO_WritePin(
            s_ledConfig[i].port,
            s_ledConfig[i].pin,
            (s_ledConfig[i].activeLevel == LED_ACTIVE_HIGH) ? GPIO_PIN_RESET : GPIO_PIN_SET
        );

        /* 配置 GPIO 为推挽输出 */
        GPIO_InitStruct.Pin   = s_ledConfig[i].pin;
        GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull  = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(s_ledConfig[i].port, &GPIO_InitStruct);
    }
}

/**
 * @brief  点亮指定 LED
 */
void bsp_ledn_on(BSP_LED_Index_t led)
{
    if (!_LED_IsValidIndex(led)) { return; }

    HAL_GPIO_WritePin(
        s_ledConfig[led].port,
        s_ledConfig[led].pin,
        (s_ledConfig[led].activeLevel == LED_ACTIVE_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}

/**
 * @brief  熄灭指定 LED
 */
void bsp_ledn_off(BSP_LED_Index_t led)
{
    if (!_LED_IsValidIndex(led)) { return; }

    HAL_GPIO_WritePin(
        s_ledConfig[led].port,
        s_ledConfig[led].pin,
        (s_ledConfig[led].activeLevel == LED_ACTIVE_HIGH) ? GPIO_PIN_RESET : GPIO_PIN_SET
    );
}

/**
 * @brief  翻转指定 LED 状态
 */
void bsp_ledn_toggle(BSP_LED_Index_t led)
{
    if (!_LED_IsValidIndex(led)) { return; }

    HAL_GPIO_TogglePin(s_ledConfig[led].port, s_ledConfig[led].pin);
}

/**
 * @brief  点亮所有 LED
 */
void bsp_ledn_allon(void)
{
    for (uint8_t i = 0U; i < (uint8_t)LED_COUNT; i++)
    {
        bsp_ledn_on((BSP_LED_Index_t)i);
    }
}

/**
 * @brief  熄灭所有 LED
 */
void bsp_ledn_alloff(void)
{
    for (uint8_t i = 0U; i < (uint8_t)LED_COUNT; i++)
    {
        bsp_ledn_off((BSP_LED_Index_t)i);
    }
}

/**
 * @brief  获取指定 LED 的当前逻辑状态
 * @retval 1 = 点亮，0 = 熄灭
 */
uint8_t bsp_ledn_getstate(BSP_LED_Index_t led)
{
    if (!_LED_IsValidIndex(led)) { return 0U; }

    GPIO_PinState pinState = HAL_GPIO_ReadPin(s_ledConfig[led].port, s_ledConfig[led].pin);

    /* 根据主动电平换算逻辑状态 */
    if (s_ledConfig[led].activeLevel == LED_ACTIVE_HIGH)
    {
        return (pinState == GPIO_PIN_SET) ? 1U : 0U;
    }
    else
    {
        return (pinState == GPIO_PIN_RESET) ? 1U : 0U;
    }
}
