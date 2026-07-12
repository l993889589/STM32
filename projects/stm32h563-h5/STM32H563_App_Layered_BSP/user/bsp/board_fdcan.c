/**
 * @file board_fdcan.c
 * @brief Industrial expansion-board dual FDCAN role binding.
 */

#include "bsp_fdcan.h"

#include "board_resources.h"
#include "bsp_dwt.h"
#include "mcu_fdcan.h"

#define BOARD_FDCAN_KERNEL_CLOCK_HZ BOARD_HSE_FREQUENCY_HZ

static mcu_fdcan_context_t board_fdcan_contexts[BOARD_FDCAN_COUNT];
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
    gpio.Alternate = BOARD_FDCAN_1_TX_AF;
    if(role == BOARD_FDCAN_FIELD_1)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
        gpio.Pin = BOARD_FDCAN_1_TX_PIN;
        HAL_GPIO_Init(BOARD_FDCAN_1_TX_PORT, &gpio);
        gpio.Pin = BOARD_FDCAN_1_RX_PIN;
        gpio.Alternate = BOARD_FDCAN_1_RX_AF;
        HAL_GPIO_Init(BOARD_FDCAN_1_RX_PORT, &gpio);
        HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, BOARD_FDCAN_IRQ_PRIORITY, 0U);
        HAL_NVIC_SetPriority(FDCAN1_IT1_IRQn, BOARD_FDCAN_IRQ_PRIORITY, 0U);
        *instance = BOARD_FDCAN_1_INSTANCE;
        return BSP_STATUS_OK;
    }
    if(role == BOARD_FDCAN_FIELD_2)
    {
        gpio.Alternate = BOARD_FDCAN_2_TX_AF;
        gpio.Pin = BOARD_FDCAN_2_TX_PIN | BOARD_FDCAN_2_RX_PIN;
        HAL_GPIO_Init(BOARD_FDCAN_2_TX_PORT, &gpio);
        HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, BOARD_FDCAN_IRQ_PRIORITY, 0U);
        HAL_NVIC_SetPriority(FDCAN2_IT1_IRQn, BOARD_FDCAN_IRQ_PRIORITY, 0U);
        *instance = BOARD_FDCAN_2_INSTANCE;
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
    status = mcu_fdcan_init(&board_fdcan_contexts[role],
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
           mcu_fdcan_stop(&board_fdcan_contexts[role]);
}

/** @brief Implement bsp_fdcan_recover() for a logical role. */
bsp_status_t bsp_fdcan_recover(board_fdcan_role_t role)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           mcu_fdcan_recover(&board_fdcan_contexts[role]);
}

/** @brief Cancel all pending transmissions for one logical role. */
bsp_status_t bsp_fdcan_abort_transmit(board_fdcan_role_t role)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           mcu_fdcan_abort_transmit(&board_fdcan_contexts[role]);
}

/** @brief Override one TX pin briefly to create a real physical bit error. */
bsp_status_t bsp_fdcan_inject_dominant_pulse(board_fdcan_role_t role,
                                             uint32_t pulse_us)
{
    GPIO_InitTypeDef gpio = {0};
    FDCAN_GlobalTypeDef *ignored_instance = NULL;
    GPIO_TypeDef *port;
    uint16_t pin;

    if((role >= BOARD_FDCAN_COUNT) || (pulse_us == 0U) ||
       (pulse_us > 100U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    port = role == BOARD_FDCAN_FIELD_1 ?
           BOARD_FDCAN_1_TX_PORT : BOARD_FDCAN_2_TX_PORT;
    pin = role == BOARD_FDCAN_FIELD_1 ?
          BOARD_FDCAN_1_TX_PIN : BOARD_FDCAN_2_TX_PIN;
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &gpio);
    bsp_dwt_delay_us(pulse_us);
    return board_fdcan_hardware_init(role, &ignored_instance);
}

/** @brief Implement bsp_fdcan_send() for a logical role. */
bsp_status_t bsp_fdcan_send(board_fdcan_role_t role,
                            const bsp_fdcan_frame_t *frame)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           mcu_fdcan_send(&board_fdcan_contexts[role], frame);
}

/** @brief Implement bsp_fdcan_try_receive() for a logical role. */
bsp_status_t bsp_fdcan_try_receive(board_fdcan_role_t role,
                                   bsp_fdcan_frame_t *frame,
                                   bool *has_frame)
{
    return role >= BOARD_FDCAN_COUNT ? BSP_STATUS_INVALID_ARGUMENT :
           mcu_fdcan_try_receive(&board_fdcan_contexts[role],
                                         frame,
                                         has_frame);
}

/** @brief Implement bsp_fdcan_get_health() for a logical role. */
bsp_status_t bsp_fdcan_get_health(board_fdcan_role_t role,
                                       bsp_fdcan_health_t *health)
{
    if((role >= BOARD_FDCAN_COUNT) || (health == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    return mcu_fdcan_get_health(&board_fdcan_contexts[role], health);
}

/** @brief Dispatch FDCAN1 interrupt line 0. */
void FDCAN1_IT0_IRQHandler(void)
{
    mcu_fdcan_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_1]);
}

/** @brief Dispatch FDCAN1 interrupt line 1. */
void FDCAN1_IT1_IRQHandler(void)
{
    mcu_fdcan_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_1]);
}

/** @brief Dispatch FDCAN2 interrupt line 0. */
void FDCAN2_IT0_IRQHandler(void)
{
    mcu_fdcan_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_2]);
}

/** @brief Dispatch FDCAN2 interrupt line 1. */
void FDCAN2_IT1_IRQHandler(void)
{
    mcu_fdcan_irq(&board_fdcan_contexts[BOARD_FDCAN_FIELD_2]);
}
