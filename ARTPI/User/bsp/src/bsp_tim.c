#include "bsp.h"

#define BSP_TIM_INSTANCE     TIM2
#define BSP_TIM_IRQn         TIM2_IRQn
#define BSP_TIM_IRQ_PRIORITY 5U

static TIM_HandleTypeDef tim_handle;
static bsp_tim_callback_t tim_callbacks[4];
static void *tim_arguments[4];
static uint8_t tim_initialized;

static uint32_t bsp_tim_get_clock(void);
static HAL_StatusTypeDef bsp_tim_get_channel_resources(bsp_tim_channel_t channel,
                                                        volatile uint32_t **compare,
                                                        uint32_t *interrupt);
static void bsp_tim_handle_interrupt(uint32_t index, uint32_t interrupt);

HAL_StatusTypeDef bsp_tim_init(void)
{
    uint32_t timer_clock;

    __HAL_RCC_TIM2_CLK_ENABLE();
    timer_clock = bsp_tim_get_clock();
    if ((timer_clock < 1000000U) || ((timer_clock % 1000000U) != 0U))
    {
        return HAL_ERROR;
    }

    tim_handle.Instance = BSP_TIM_INSTANCE;
    tim_handle.Init.Prescaler = (timer_clock / 1000000U) - 1U;
    tim_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim_handle.Init.Period = UINT32_MAX;
    tim_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    tim_handle.Init.RepetitionCounter = 0U;
    tim_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&tim_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    CLEAR_BIT(BSP_TIM_INSTANCE->DIER,
              TIM_IT_CC1 | TIM_IT_CC2 | TIM_IT_CC3 | TIM_IT_CC4);
    WRITE_REG(BSP_TIM_INSTANCE->SR, 0U);
    WRITE_REG(BSP_TIM_INSTANCE->CNT, 0U);

    HAL_NVIC_SetPriority(BSP_TIM_IRQn, BSP_TIM_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BSP_TIM_IRQn);

    if (HAL_TIM_Base_Start(&tim_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    tim_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_tim_start(bsp_tim_channel_t channel,
                                uint32_t delay_us,
                                bsp_tim_callback_t callback,
                                void *argument)
{
    volatile uint32_t *compare;
    uint32_t interrupt;
    uint32_t index;
    uint32_t interrupt_state;

    if ((tim_initialized == 0U) || (delay_us < BSP_TIM_MIN_DELAY_US) ||
        (callback == NULL) ||
        (bsp_tim_get_channel_resources(channel, &compare, &interrupt) != HAL_OK))
    {
        return HAL_ERROR;
    }

    index = (uint32_t)channel - 1U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();

    if ((BSP_TIM_INSTANCE->DIER & interrupt) != 0U)
    {
        __set_PRIMASK(interrupt_state);
        return HAL_BUSY;
    }

    tim_callbacks[index] = callback;
    tim_arguments[index] = argument;
    CLEAR_BIT(BSP_TIM_INSTANCE->SR, interrupt);
    *compare = BSP_TIM_INSTANCE->CNT + delay_us;
    SET_BIT(BSP_TIM_INSTANCE->DIER, interrupt);

    __set_PRIMASK(interrupt_state);
    return HAL_OK;
}

HAL_StatusTypeDef bsp_tim_stop(bsp_tim_channel_t channel)
{
    volatile uint32_t *compare;
    uint32_t interrupt;
    uint32_t index;
    uint32_t interrupt_state;

    if ((tim_initialized == 0U) ||
        (bsp_tim_get_channel_resources(channel, &compare, &interrupt) != HAL_OK))
    {
        return HAL_ERROR;
    }

    (void)compare;
    index = (uint32_t)channel - 1U;
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    CLEAR_BIT(BSP_TIM_INSTANCE->DIER, interrupt);
    CLEAR_BIT(BSP_TIM_INSTANCE->SR, interrupt);
    tim_callbacks[index] = NULL;
    tim_arguments[index] = NULL;
    __set_PRIMASK(interrupt_state);

    return HAL_OK;
}

uint32_t bsp_tim_now_us(void)
{
    return (tim_initialized != 0U) ? BSP_TIM_INSTANCE->CNT : 0U;
}

static uint32_t bsp_tim_get_clock(void)
{
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();

    if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != RCC_D2CFGR_D2PPRE1_DIV1)
    {
        timer_clock *= 2U;
    }

    return timer_clock;
}

static HAL_StatusTypeDef bsp_tim_get_channel_resources(bsp_tim_channel_t channel,
                                                        volatile uint32_t **compare,
                                                        uint32_t *interrupt)
{
    if ((compare == NULL) || (interrupt == NULL))
    {
        return HAL_ERROR;
    }

    if (channel == BSP_TIM_CHANNEL_1)
    {
        *compare = &BSP_TIM_INSTANCE->CCR1;
        *interrupt = TIM_IT_CC1;
    }
    else if (channel == BSP_TIM_CHANNEL_2)
    {
        *compare = &BSP_TIM_INSTANCE->CCR2;
        *interrupt = TIM_IT_CC2;
    }
    else if (channel == BSP_TIM_CHANNEL_3)
    {
        *compare = &BSP_TIM_INSTANCE->CCR3;
        *interrupt = TIM_IT_CC3;
    }
    else if (channel == BSP_TIM_CHANNEL_4)
    {
        *compare = &BSP_TIM_INSTANCE->CCR4;
        *interrupt = TIM_IT_CC4;
    }
    else
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static void bsp_tim_handle_interrupt(uint32_t index, uint32_t interrupt)
{
    bsp_tim_callback_t callback;
    void *argument;

    if (((BSP_TIM_INSTANCE->SR & interrupt) == 0U) ||
        ((BSP_TIM_INSTANCE->DIER & interrupt) == 0U))
    {
        return;
    }

    CLEAR_BIT(BSP_TIM_INSTANCE->DIER, interrupt);
    CLEAR_BIT(BSP_TIM_INSTANCE->SR, interrupt);
    callback = tim_callbacks[index];
    argument = tim_arguments[index];
    tim_callbacks[index] = NULL;
    tim_arguments[index] = NULL;

    if (callback != NULL)
    {
        callback(argument);
    }
}

void TIM2_IRQHandler(void)
{
    bsp_tim_handle_interrupt(0U, TIM_IT_CC1);
    bsp_tim_handle_interrupt(1U, TIM_IT_CC2);
    bsp_tim_handle_interrupt(2U, TIM_IT_CC3);
    bsp_tim_handle_interrupt(3U, TIM_IT_CC4);
}
