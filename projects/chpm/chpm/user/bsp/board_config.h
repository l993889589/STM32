/**
 * @file board_config.h
 * @brief CHPM board resource, pin, clock and interrupt assignments.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "stm32f4xx_hal.h"

#define BOARD_NAME                         "F401CCU6-CHPM"
#define BOARD_UART_BAUD_RATE               115200U
#define BOARD_UART_DMA_RX_BUFFER_SIZE      128U
#define BOARD_IRQ_PRIORITY_COMMUNICATION   5U

#define BOARD_MODBUS_UART                  USART1
#define BOARD_MODBUS_UART_GPIO_PORT        GPIOA
#define BOARD_MODBUS_UART_GPIO_PINS        (GPIO_PIN_9 | GPIO_PIN_10)
#define BOARD_MODBUS_UART_GPIO_AF          GPIO_AF7_USART1
#define BOARD_MODBUS_UART_DMA              DMA2_Stream2
#define BOARD_MODBUS_UART_DMA_CHANNEL      DMA_CHANNEL_4
#define BOARD_MODBUS_UART_DMA_IRQn         DMA2_Stream2_IRQn

#define BOARD_DWIN_UART                    USART2
#define BOARD_DWIN_UART_GPIO_PORT          GPIOA
#define BOARD_DWIN_UART_GPIO_PINS          (GPIO_PIN_2 | GPIO_PIN_3)
#define BOARD_DWIN_UART_GPIO_AF            GPIO_AF7_USART2
#define BOARD_DWIN_UART_DMA                DMA1_Stream5
#define BOARD_DWIN_UART_DMA_CHANNEL        DMA_CHANNEL_4
#define BOARD_DWIN_UART_DMA_IRQn           DMA1_Stream5_IRQn

#define BOARD_STATUS_GPIO_PORT             GPIOC
#define BOARD_STATUS_GPIO_PIN              GPIO_PIN_13
#define BOARD_CONTROL_GPIO_PORT            GPIOB
#define BOARD_CONTROL_GPIO_PINS            (GPIO_PIN_14 | GPIO_PIN_15)

#define BOARD_FAN_PWM_TIMER                TIM1
#define BOARD_FAN_PWM_CHANNEL              TIM_CHANNEL_1
#define BOARD_FAN_PWM_GPIO_PORT            GPIOA
#define BOARD_FAN_PWM_GPIO_PIN             GPIO_PIN_8
#define BOARD_FAN_PWM_GPIO_AF              GPIO_AF1_TIM1
#define BOARD_FAN_PWM_FREQUENCY_HZ         25000U

#define BOARD_DELAY_TIMER                  TIM4
#define BOARD_DELAY_TIMER_PRESCALER        83U

/* Enable only after watchdog timing and recovery are verified on hardware. */
#define BOARD_IWDG_ENABLE                  0U
#define BOARD_IWDG_PRESCALER               IWDG_PRESCALER_64
#define BOARD_IWDG_RELOAD                  1250U

#endif
