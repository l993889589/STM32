/**
 * @file drv_modbus_port.c
 * @brief Strict Modbus RTU framing over timestamped circular-DMA UART input.
 */

#include "drv_modbus_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "bsp_uart.h"
#include "ld_modbus.h"
#include "ld_modbus_rtu_framer.h"
#include "stm32f4xx.h"
#include "tx_api.h"

#define MODBUS_BITS_PER_CHARACTER 10U
#define MODBUS_EVENT_RX           (1UL << 0)
#define MODBUS_EVENT_ERROR        (1UL << 1)

static ld_modbus_rtu_framer_t modbus_framer;
static uint8_t modbus_active_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t modbus_ready_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static TX_EVENT_FLAGS_GROUP modbus_events;
static drv_modbus_port_diagnostics_t modbus_diagnostics;
static uint32_t modbus_character_cycles;
static uint32_t modbus_event_first_byte_cycles;
static uint16_t modbus_event_byte_index;
static uint16_t modbus_last_dma_position;
static bool modbus_initialized;

/** @brief Save and mask interrupts around framer state shared with the ISR. */
static uint32_t modbus_irq_lock(void)
{
    uint32_t state = __get_PRIMASK();

    __disable_irq();
    return state;
}

/** @brief Restore the exact interrupt mask captured by modbus_irq_lock(). */
static void modbus_irq_unlock(uint32_t state)
{
    __set_PRIMASK(state);
}

/** @brief Convert a millisecond timeout to a non-early ThreadX wait. */
static ULONG modbus_timeout_ticks(uint32_t timeout_ms)
{
    uint64_t ticks;

    if(timeout_ms == 0U)
        return TX_NO_WAIT;
    ticks = ((uint64_t)timeout_ms * TX_TIMER_TICKS_PER_SECOND + 999ULL) /
            1000ULL;
    if(ticks == 0U)
        ticks = 1U;
    return ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (ULONG)ticks;
}

/** @brief Convert remaining DWT cycles to a non-early ThreadX wait. */
static ULONG modbus_gap_ticks(uint32_t remaining_cycles)
{
    uint64_t ticks;

    if(SystemCoreClock == 0U)
        return 1U;
    ticks = ((uint64_t)remaining_cycles * TX_TIMER_TICKS_PER_SECOND +
             SystemCoreClock - 1U) /
            SystemCoreClock;

    if(ticks == 0U)
        ticks = 1U;
    return ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (ULONG)ticks;
}

/** @brief Feed every DMA byte with an interpolated end-of-character timestamp. */
static void modbus_rx(const uint8_t *data,
                      uint16_t length,
                      const bsp_uart_rx_info_t *info,
                      void *context)
{
    uint32_t last_byte_end_cycles;
    uint32_t timestamp_cycles;
    uint16_t index;

    (void)context;
    if(!modbus_initialized || data == NULL || length == 0U || info == NULL ||
       info->event_length == 0U)
    {
        modbus_diagnostics.invalid_rx_metadata++;
        return;
    }

    if(info->first_segment)
    {
        last_byte_end_cycles = info->timestamp_cycles;
        if(info->event == BSP_UART_RX_EVENT_IDLE)
            last_byte_end_cycles -= modbus_character_cycles;
        modbus_event_first_byte_cycles =
            last_byte_end_cycles -
            ((uint32_t)info->event_length - 1U) * modbus_character_cycles;
        modbus_event_byte_index = 0U;
    }
    if((uint32_t)modbus_event_byte_index + length > info->event_length)
    {
        modbus_diagnostics.invalid_rx_metadata++;
        ld_modbus_rtu_framer_on_error(&modbus_framer,
                                      info->timestamp_cycles);
        modbus_event_byte_index = 0U;
        (void)tx_event_flags_set(&modbus_events, MODBUS_EVENT_ERROR, TX_OR);
        return;
    }

    for(index = 0U; index < length; index++)
    {
        timestamp_cycles = modbus_event_first_byte_cycles +
                           (uint32_t)modbus_event_byte_index *
                               modbus_character_cycles;
        ld_modbus_rtu_framer_on_byte(&modbus_framer,
                                     data[index],
                                     timestamp_cycles);
        modbus_event_byte_index++;
    }

    modbus_diagnostics.rx_chunks++;
    modbus_diagnostics.rx_bytes += length;
    if(info->last_segment)
    {
        modbus_last_dma_position =
            bsp_uart_rx_dma_position(BSP_UART_MODBUS);
        modbus_event_byte_index = 0U;
        (void)tx_event_flags_set(&modbus_events, MODBUS_EVENT_RX, TX_OR);
    }
}

/** @brief Invalidate the active RTU stream after a UART/DMA receive error. */
static void modbus_error(void *context)
{
    (void)context;
    ld_modbus_rtu_framer_on_error(&modbus_framer, DWT->CYCCNT);
    modbus_event_byte_index = 0U;
    modbus_diagnostics.uart_resets++;
    (void)tx_event_flags_set(&modbus_events, MODBUS_EVENT_ERROR, TX_OR);
}

/** @brief Initialize strict T1.5/T3.5 framing and the ISR-to-thread event. */
bsp_status_t drv_modbus_port_init(uint32_t baud_rate)
{
    bsp_status_t status;

    if(modbus_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;
    if(baud_rate == 0U || SystemCoreClock == 0U ||
       bsp_uart_baud_rate(BSP_UART_MODBUS) != baud_rate)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!ld_modbus_rtu_framer_init(&modbus_framer,
                                   modbus_active_frame,
                                   modbus_ready_frame,
                                   sizeof(modbus_active_frame),
                                   baud_rate,
                                   MODBUS_BITS_PER_CHARACTER,
                                   SystemCoreClock))
        return BSP_STATUS_INVALID_ARGUMENT;
    if(tx_event_flags_create(&modbus_events, "Modbus RTU RX") != TX_SUCCESS)
        return BSP_STATUS_IO_ERROR;

    modbus_character_cycles = modbus_framer.timing.char_ticks;
    modbus_last_dma_position =
        bsp_uart_rx_dma_position(BSP_UART_MODBUS);
    modbus_initialized = true;
    status = bsp_uart_set_callbacks(BSP_UART_MODBUS,
                                    modbus_rx,
                                    modbus_error,
                                    NULL);
    if(status != BSP_STATUS_OK)
    {
        modbus_initialized = false;
        (void)tx_event_flags_delete(&modbus_events);
    }
    return status;
}

/** @brief Wait for one strict RTU frame without a periodic 1 ms poll loop. */
int drv_modbus_port_read_frame(uint8_t *data,
                               uint16_t capacity,
                               uint32_t timeout_ms)
{
    ULONG actual_flags;
    ULONG elapsed_ticks;
    ULONG remaining_ticks;
    ULONG wait_ticks;
    ULONG timeout_ticks;
    ULONG start_ticks;
    uint16_t frame_length;
    uint16_t dma_position;
    uint32_t now_cycles;
    uint32_t poll_threshold_cycles;
    uint32_t silence_cycles;
    uint32_t state;

    if(!modbus_initialized || data == NULL || capacity == 0U)
        return -1;
    timeout_ticks = modbus_timeout_ticks(timeout_ms);
    start_ticks = tx_time_get();

    for(;;)
    {
        state = modbus_irq_lock();
        dma_position = bsp_uart_rx_dma_position(BSP_UART_MODBUS);
        if(dma_position == modbus_last_dma_position)
        {
            now_cycles = DWT->CYCCNT;
            ld_modbus_rtu_framer_poll(&modbus_framer, now_cycles);
        }
        else
        {
            /*
             * DMA owns bytes that the UART callback has not timestamped yet.
             * Do not advance protocol time past those bytes. The UART
             * callback must first publish their earlier completion stamps.
             */
            now_cycles = modbus_framer.timing.last_byte_ticks;
        }
        if(modbus_framer.ready_available &&
           modbus_framer.ready_length > capacity)
        {
            modbus_irq_unlock(state);
            return -1;
        }
        if(ld_modbus_rtu_framer_take(&modbus_framer,
                                      data,
                                      capacity,
                                      &frame_length))
        {
            modbus_irq_unlock(state);
            return (int)frame_length;
        }

        if(timeout_ticks == TX_NO_WAIT)
        {
            modbus_irq_unlock(state);
            return 0;
        }
        elapsed_ticks = tx_time_get() - start_ticks;
        if(elapsed_ticks >= timeout_ticks)
        {
            modbus_irq_unlock(state);
            return 0;
        }
        remaining_ticks = timeout_ticks - elapsed_ticks;
        wait_ticks = remaining_ticks;
        if(dma_position == modbus_last_dma_position &&
           modbus_framer.active_open)
        {
            poll_threshold_cycles = modbus_framer.timing.char_ticks +
                                    modbus_framer.timing.t35_ticks;
            silence_cycles = now_cycles -
                             modbus_framer.timing.last_byte_ticks;
            if(silence_cycles < poll_threshold_cycles)
            {
                ULONG gap_wait = modbus_gap_ticks(poll_threshold_cycles -
                                                  silence_cycles);
                if(gap_wait < wait_ticks)
                    wait_ticks = gap_wait;
            }
        }
        modbus_irq_unlock(state);

        (void)tx_event_flags_get(&modbus_events,
                                 MODBUS_EVENT_RX | MODBUS_EVENT_ERROR,
                                 TX_OR_CLEAR,
                                 &actual_flags,
                                 wait_ticks);
    }
}

/** @brief Transmit one complete RTU response with a bounded UART wait. */
bsp_status_t drv_modbus_port_write(const uint8_t *data,
                                   uint16_t length,
                                   uint32_t timeout_ms)
{
    return bsp_uart_write(BSP_UART_MODBUS, data, length, timeout_ms);
}

/** @brief Copy strict framer and port diagnostics under the ISR lock. */
bool drv_modbus_port_get_diagnostics(
    ld_modbus_rtu_framer_diag_t *framer,
    drv_modbus_port_diagnostics_t *port)
{
    uint32_t state;

    if(!modbus_initialized || framer == NULL || port == NULL)
        return false;
    state = modbus_irq_lock();
    *framer = modbus_framer.diag;
    *port = modbus_diagnostics;
    modbus_irq_unlock(state);
    return true;
}
