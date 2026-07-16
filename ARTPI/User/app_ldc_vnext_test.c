/**
 * @file app_ldc_vnext_test.c
 * @brief UART4 interrupt-to-task acceptance test for the two-file LDC vNext.
 */

#include "app_ldc_vnext_test.h"

#include "bsp_uart.h"
#include "includes.h"
#include "ldc.h"

#include <stdio.h>
#include <string.h>

#define APP_LDC_TEST_TASK_PRIORITY 6U
#define APP_LDC_TEST_TASK_STACK_SIZE 1536U
#define APP_LDC_TEST_SLOT_COUNT 4U
#define APP_LDC_TEST_FRAME_CAPACITY 128U

static TX_THREAD app_ldc_test_task;
static uint64_t app_ldc_test_stack[
    APP_LDC_TEST_TASK_STACK_SIZE / sizeof(uint64_t)];

static ldc_t app_ldc_uart4 = LDC_CONTEXT_INITIALIZER;
static ldc_slot_t app_ldc_slots[APP_LDC_TEST_SLOT_COUNT];
static uint8_t app_ldc_storage[
    APP_LDC_TEST_SLOT_COUNT * APP_LDC_TEST_FRAME_CAPACITY];
static uint8_t app_ldc_frame[APP_LDC_TEST_FRAME_CAPACITY];

static volatile uint32_t app_ldc_rx_reject_events;
static volatile uint32_t app_ldc_rx_loss_events;

static void app_ldc_test_task_entry(ULONG thread_input);
static void app_ldc_uart4_receive(const uint8_t *data,
                                  uint16_t length,
                                  uint32_t end_timestamp_ticks,
                                  uint32_t event_flags,
                                  void *argument);
static ldc_lock_state_t app_ldc_lock(void *argument);
static void app_ldc_unlock(void *argument, ldc_lock_state_t state);
static void app_ldc_write_frame(const uint8_t *data, size_t length);
static void app_ldc_write_stats(void);

/** @brief Initialize LDC, bind UART4 RX, and create the consumer task. */
HAL_StatusTypeDef app_ldc_vnext_test_start(void)
{
    static const uint8_t delimiter[] = {'\r', '\n'};
    ldc_config_t config;
    UINT tx_status;

    (void)memset(&config, 0, sizeof(config));
    config.storage = app_ldc_storage;
    config.storage_size = sizeof(app_ldc_storage);
    config.slots = app_ldc_slots;
    config.slot_count = APP_LDC_TEST_SLOT_COUNT;
    config.frame_capacity = APP_LDC_TEST_FRAME_CAPACITY;
    config.frame_mode = LDC_FRAME_MODE_DELIMITER;
    config.full_policy = LDC_FULL_REJECT_NEW;
    config.delimiter = delimiter;
    config.delimiter_length = (uint8_t)sizeof(delimiter);
    config.include_delimiter = 0U;
    config.emit_empty_frames = 0U;
    config.lock = app_ldc_lock;
    config.unlock = app_ldc_unlock;

    if (ldc_init(&app_ldc_uart4, &config) != LDC_STATUS_OK)
    {
        return HAL_ERROR;
    }
    if (bsp_uart_receive_start(BSP_UART_DEBUG,
                               app_ldc_uart4_receive,
                               NULL) != HAL_OK)
    {
        (void)ldc_deinit(&app_ldc_uart4);
        return HAL_ERROR;
    }

    tx_status = tx_thread_create(&app_ldc_test_task,
                                 "ldc_uart4_test",
                                 app_ldc_test_task_entry,
                                 0UL,
                                 app_ldc_test_stack,
                                 sizeof(app_ldc_test_stack),
                                 APP_LDC_TEST_TASK_PRIORITY,
                                 APP_LDC_TEST_TASK_PRIORITY,
                                 TX_NO_TIME_SLICE,
                                 TX_AUTO_START);
    if (tx_status != TX_SUCCESS)
    {
        (void)bsp_uart_receive_stop(BSP_UART_DEBUG);
        (void)ldc_deinit(&app_ldc_uart4);
        return HAL_ERROR;
    }

    return HAL_OK;
}

/** @brief Feed each interrupt-delivered UART block into one LDC transaction. */
static void app_ldc_uart4_receive(const uint8_t *data,
                                  uint16_t length,
                                  uint32_t end_timestamp_ticks,
                                  uint32_t event_flags,
                                  void *argument)
{
    ldc_write_result_t result;

    (void)argument;
    (void)end_timestamp_ticks;
    if ((event_flags & BSP_UART_RX_EVENT_ERROR_MASK) != 0U)
    {
        (void)ldc_rx_abort(&app_ldc_uart4);
        app_ldc_rx_loss_events++;
        return;
    }
    if (event_flags != BSP_UART_RX_EVENT_DATA)
    {
        app_ldc_rx_reject_events++;
        return;
    }
    result = ldc_rx_write(&app_ldc_uart4, data, length);
    if (result.accepted_bytes != length)
    {
        app_ldc_rx_reject_events++;
    }
    if (result.status == LDC_STATUS_DATA_LOSS)
    {
        app_ldc_rx_loss_events++;
    }
}

/** @brief Consume completed frames from ThreadX task context. */
static void app_ldc_test_task_entry(ULONG thread_input)
{
    static const uint8_t stats_command[] = "LDC-VNEXT-STATS";

    (void)thread_input;
    bsp_uart_write_string(BSP_UART_DEBUG,
                          "[LDC_VNEXT] READY uart=UART4 baud=115200 "
                          "mode=CRLF slots=4 capacity=128\r\n");

    while (1)
    {
        size_t length = 0U;

        while (ldc_frame_read(&app_ldc_uart4,
                              app_ldc_frame,
                              sizeof(app_ldc_frame),
                              &length) == LDC_STATUS_OK)
        {
            app_ldc_write_frame(app_ldc_frame, length);
            if ((length == (sizeof(stats_command) - 1U)) &&
                (memcmp(app_ldc_frame,
                        stats_command,
                        sizeof(stats_command) - 1U) == 0))
            {
                app_ldc_write_stats();
            }
        }
        (void)tx_thread_sleep(1U);
    }
}

/** @brief Echo a frame without performing UART output from interrupt context. */
static void app_ldc_write_frame(const uint8_t *data, size_t length)
{
    char prefix[48];

    (void)snprintf(prefix,
                   sizeof(prefix),
                   "[LDC_VNEXT] FRAME len=%lu data=",
                   (unsigned long)length);
    bsp_uart_write_string(BSP_UART_DEBUG, prefix);
    (void)bsp_uart_write(BSP_UART_DEBUG, data, length);
    bsp_uart_write_string(BSP_UART_DEBUG, "\r\n");
}

/** @brief Print a stable diagnostics line for the PC acceptance script. */
static void app_ldc_write_stats(void)
{
    ldc_stats_t stats;
    char message[256];

    if (ldc_get_stats(&app_ldc_uart4, &stats) != LDC_STATUS_OK)
    {
        return;
    }

    (void)snprintf(message,
                   sizeof(message),
                   "[LDC_VNEXT] STATS accepted=%llu committed=%llu "
                   "consumed=%llu dropped=%llu overflow=%llu rejected=%llu "
                   "isr_reject=%lu isr_loss=%lu\r\n",
                   (unsigned long long)stats.accepted_bytes,
                   (unsigned long long)stats.committed_frames,
                   (unsigned long long)stats.consumed_frames,
                   (unsigned long long)stats.dropped_frames,
                   (unsigned long long)stats.overflow_frames,
                   (unsigned long long)stats.rejected_bytes,
                   (unsigned long)app_ldc_rx_reject_events,
                   (unsigned long)app_ldc_rx_loss_events);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}

/** @brief Preserve PRIMASK while protecting only LDC metadata transitions. */
static ldc_lock_state_t app_ldc_lock(void *argument)
{
    uint32_t state = __get_PRIMASK();

    (void)argument;
    __disable_irq();
    __DMB();
    return (ldc_lock_state_t)state;
}

/** @brief Restore the exact interrupt mask captured by app_ldc_lock(). */
static void app_ldc_unlock(void *argument, ldc_lock_state_t state)
{
    (void)argument;
    __DMB();
    if ((uint32_t)state == 0U)
    {
        __enable_irq();
    }
}
