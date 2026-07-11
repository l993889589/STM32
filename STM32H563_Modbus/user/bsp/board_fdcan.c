/**
 * @file board_fdcan.c
 * @brief Industrial expansion-board dual FDCAN role binding.
 */

#include "bsp_fdcan.h"

#include "bsp_fdcan_stm32h5.h"
#include "stm32h5xx_hal.h"

#define BOARD_FDCAN_KERNEL_CLOCK_HZ 25000000U

static bsp_fdcan_stm32h5_context_t board_fdcan_contexts[BOARD_FDCAN_COUNT];
static bool board_fdcan_clock_configured;

/** @brief Select the stable 25 MHz HSE as the common FDCAN kernel clock. */
static bsp_status_t board_fdcan_configure_kernel_clock(void)
{
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if(board_fdcan_clock_configured)
    {
        return BSP_STATUS_OK;
    }
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    peripheral_clock.FdcanClockSelection = RCC_FDCANCLKSOURCE_HSE;
    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    __HAL_RCC_FDCAN_CLK_ENABLE();
    board_fdcan_clock_configured = true;
    return BSP_STATUS_OK;
}

/** @brief Configure the exact schematic pins and interrupts for one FDCAN role. */
static bsp_status_t board_fdcan_hardware_init(board_fdcan_role_t role,
                                              FDCAN_GlobalTypeDef **instance)
{
    GPIO_InitTypeDef gpio = {0};
    bsp_status_t status;

    if(instance == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = board_fdcan_configure_kernel_clock();
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_FDCAN1;
    if(role == BOARD_FDCAN_FIELD_1)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_7;
        HAL_GPIO_Init(GPIOB, &gpio);
        gpio.Pin = GPIO_PIN_0;
        HAL_GPIO_Init(GPIOE, &gpio);
        HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 8U, 0U);
        HAL_NVIC_SetPriority(FDCAN1_IT1_IRQn, 8U, 0U);
        *instance = FDCAN1;
        return BSP_STATUS_OK;
    }
    if(role == BOARD_FDCAN_FIELD_2)
    {
        gpio.Alternate = GPIO_AF9_FDCAN2;
        gpio.Pin = GPIO_PIN_6 | GPIO_PIN_12;
        HAL_GPIO_Init(GPIOB, &gpio);
        HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, 8U, 0U);
        HAL_NVIC_SetPriority(FDCAN2_IT1_IRQn, 8U, 0U);
        *instance = FDCAN2;
        return BSP_STATUS_OK;
    }
    return BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement bsp_fdcan_init() for the two schematic FDCAN channels. */
bsp_status_t bsp_fdcan_init(board_fdcan_role_t role,
                            const bsp_fdcan_config_t *config)
{
    FDCAN_GlobalTypeDef *instance = NULL;
    bsp_status_t status;

    if((role >= BOARD_FDCAN_COUNT) || (config == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = board_fdcan_hardware_init(role, &instance);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = bsp_fdcan_stm32h5_init(&board_fdcan_contexts[role],
                                    instance,
                                    BOARD_FDCAN_KERNEL_CLOCK_HZ,
                                    config);
    if(status == BSP_STATUS_OK)
    {
        IRQn_Type interrupt_0 = role == BOARD_FDCAN_FIELD_1 ?
                                FDCAN1_IT0_IRQn : FDCAN2_IT0_IRQn;
        IRQn_Type interrupt_1 = role == BOARD_FDCAN_FIELD_1 ?
                                FDCAN1_IT1_IRQn : FDCAN2_IT1_IRQn;

        HAL_NVIC_EnableIRQ(interrupt_0);
        HAL_NVIC_EnableIRQ(interrupt_1);
    }
    return status;
}

/** @brief Implement bsp_fdcan_stop() for a logical role. */
bsp_status_t bsp_fdcan_stop(board_fdcan_role_t role)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_fdcan_stm32h5_stop(&board_fdcan_contexts[role]);
}

/** @brief Implement bsp_fdcan_recover() for a logical role. */
bsp_status_t bsp_fdcan_recover(board_fdcan_role_t role)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_fdcan_stm32h5_recover(&board_fdcan_contexts[role]);
}

/** @brief Implement bsp_fdcan_send() for a logical role. */
bsp_status_t bsp_fdcan_send(board_fdcan_role_t role,
                            const bsp_fdcan_frame_t *frame)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_fdcan_stm32h5_send(&board_fdcan_contexts[role], frame);
}

/** @brief Implement bsp_fdcan_try_receive() for a logical role. */
bsp_status_t bsp_fdcan_try_receive(board_fdcan_role_t role,
                                   bsp_fdcan_frame_t *frame,
                                   bool *has_frame)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_fdcan_stm32h5_try_receive(&board_fdcan_contexts[role],
                                         frame,
                                         has_frame);
}

/** @brief Implement bsp_fdcan_get_diagnostics() for a logical role. */
bsp_status_t bsp_fdcan_get_diagnostics(board_fdcan_role_t role,
                                       bsp_fdcan_diagnostics_t *diagnostics)
{
    if((role >= BOARD_FDCAN_COUNT) || (diagnostics == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *diagnostics = board_fdcan_contexts[role].diagnostics;
    return board_fdcan_contexts[role].is_initialized ?
           BSP_STATUS_OK : BSP_STATUS_NOT_READY;
}

/** @brief Dispatch FDCAN1 interrupt line 0. */
void FDCAN1_IT0_IRQHandler(void)
{
    bsp_fdcan_stm32h5_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_1]);
}

/** @brief Dispatch FDCAN1 interrupt line 1. */
void FDCAN1_IT1_IRQHandler(void)
{
    bsp_fdcan_stm32h5_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_1]);
}

/** @brief Dispatch FDCAN2 interrupt line 0. */
void FDCAN2_IT0_IRQHandler(void)
{
    bsp_fdcan_stm32h5_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_2]);
}

/** @brief Dispatch FDCAN2 interrupt line 1. */
void FDCAN2_IT1_IRQHandler(void)
{
    bsp_fdcan_stm32h5_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_2]);
}
