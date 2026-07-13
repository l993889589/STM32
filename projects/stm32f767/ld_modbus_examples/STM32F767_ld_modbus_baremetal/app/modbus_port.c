/**
 * @file modbus_port.c
 * @brief USART3 single-byte IRQ receive and SysTick microsecond interpolation.
 *
 * CubeMX owns USART3 pins, clock, NVIC, handle, and IRQ forwarding. This port
 * only arms one-byte HAL reception, timestamps completed characters, and moves
 * them through a static single-producer/single-consumer ring.
 */

#include "modbus_port.h"

#include <string.h>

#include "usart.h"

#define MODBUS_PORT_RX_CAPACITY (512U)

typedef struct
{
    uint8_t byte;
    uint32_t timestamp_us;
} modbus_port_rx_entry_t;

volatile modbus_port_diag_t g_modbus_port_diag;

static modbus_port_rx_entry_t g_rx_ring[MODBUS_PORT_RX_CAPACITY];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;
static uint8_t g_uart_rx_byte;
static volatile uint32_t g_last_time_us;

/** @brief Advance a receive-ring index with explicit wrap. */
static uint16_t modbus_port_next_index(uint16_t index)
{
    index++;
    return (index >= MODBUS_PORT_RX_CAPACITY) ? 0U : index;
}

/** @brief Publish one timestamped byte from USART3 interrupt context. */
static void modbus_port_push_from_isr(uint8_t byte, uint32_t timestamp_us)
{
    uint16_t head = g_rx_head;
    uint16_t next = modbus_port_next_index(head);

    if(next == g_rx_tail)
    {
        g_modbus_port_diag.ring_overflows++;
        return;
    }

    g_rx_ring[head].byte = byte;
    g_rx_ring[head].timestamp_us = timestamp_us;
    __DMB();
    g_rx_head = next;
    g_modbus_port_diag.received_bytes++;
}

/** @brief Configure USART3 and arm the first one-byte interrupt receive. */
HAL_StatusTypeDef modbus_port_init(uint32_t baud_rate)
{
    HAL_StatusTypeDef status;

    memset((void *)&g_modbus_port_diag, 0, sizeof(g_modbus_port_diag));
    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_last_time_us = 0U;

    if(huart3.Init.BaudRate != baud_rate)
    {
        huart3.Init.BaudRate = baud_rate;
        status = HAL_UART_Init(&huart3);
        if(status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_UART_Receive_IT(&huart3, &g_uart_rx_byte, 1U);
}

/** @brief Derive a monotonic wrapping microsecond value from SysTick state. */
uint32_t modbus_port_time_us(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t milliseconds;
    uint32_t ticks_per_ms;
    uint32_t current_tick;
    uint32_t elapsed_ticks;
    uint32_t now_us;

    __disable_irq();
    milliseconds = HAL_GetTick();
    ticks_per_ms = SysTick->LOAD + 1U;
    current_tick = SysTick->VAL;

    if((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U)
    {
        milliseconds++;
        current_tick = SysTick->VAL;
    }

    if(ticks_per_ms == 0U)
    {
        now_us = milliseconds * 1000U;
    }
    else
    {
        elapsed_ticks = (current_tick <= SysTick->LOAD) ?
                        (SysTick->LOAD - current_tick) : 0U;
        now_us = (milliseconds * 1000U) +
                 (uint32_t)(((uint64_t)elapsed_ticks * 1000ULL) /
                            (uint64_t)ticks_per_ms);
    }

    if((int32_t)(now_us - g_last_time_us) < 0)
    {
        now_us = g_last_time_us;
    }
    else
    {
        g_last_time_us = now_us;
    }

    if(primask == 0U)
    {
        __enable_irq();
    }

    return now_us;
}

/** @brief Pop one byte/timestamp pair from the superloop consumer side. */
bool modbus_port_try_read(uint8_t *byte, uint32_t *timestamp_us)
{
    uint16_t tail;

    if((byte == NULL) || (timestamp_us == NULL))
    {
        return false;
    }

    tail = g_rx_tail;
    if(tail == g_rx_head)
    {
        return false;
    }

    *byte = g_rx_ring[tail].byte;
    *timestamp_us = g_rx_ring[tail].timestamp_us;
    __DMB();
    g_rx_tail = modbus_port_next_index(tail);
    return true;
}

/** @brief Send one complete RTU ADU through the CubeMX USART3 handle. */
HAL_StatusTypeDef modbus_port_write(const uint8_t *data,
                                    uint16_t length,
                                    uint32_t timeout_ms)
{
    if((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(&huart3, (uint8_t *)data, length, timeout_ms);
}

/** @brief Timestamp and enqueue one completed USART3 character, then rearm. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if((huart != NULL) && (huart->Instance == USART3))
    {
        modbus_port_push_from_isr(g_uart_rx_byte, modbus_port_time_us());
        if(HAL_UART_Receive_IT(&huart3, &g_uart_rx_byte, 1U) != HAL_OK)
        {
            g_modbus_port_diag.rx_rearm_errors++;
        }
    }
}

/** @brief Record a USART3 error and deterministically restart one-byte RX. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if((huart != NULL) && (huart->Instance == USART3))
    {
        g_modbus_port_diag.uart_errors++;
        if(HAL_UART_Receive_IT(&huart3, &g_uart_rx_byte, 1U) != HAL_OK)
        {
            g_modbus_port_diag.rx_rearm_errors++;
        }
    }
}
