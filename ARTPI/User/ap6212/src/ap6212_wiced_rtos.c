#include "ap6212_wiced.h"
#include "bsp.h"
#include "stm32h7xx.h"
#include "tx_api.h"

#include <stddef.h>
#include <stdint.h>

#define AP6212_THREAD_COUNT       2U
#define AP6212_THREAD_STACK_SIZE 4096U
#define AP6212_SEMAPHORE_COUNT   12U
#define AP6212_QUEUE_COUNT        4U
#define AP6212_NEVER_TIMEOUT 0xFFFFFFFFU

typedef struct
{
    TX_THREAD control_block;
    uint64_t stack[AP6212_THREAD_STACK_SIZE / sizeof(uint64_t)];
    uint8_t allocated;
} ap6212_thread_slot_t;

typedef struct
{
    TX_SEMAPHORE control_block;
    uint8_t allocated;
} ap6212_semaphore_slot_t;

typedef struct
{
    TX_QUEUE control_block;
    uint8_t allocated;
} ap6212_queue_slot_t;

static ap6212_thread_slot_t thread_slots[AP6212_THREAD_COUNT];
static ap6212_semaphore_slot_t semaphore_slots[AP6212_SEMAPHORE_COUNT];
static ap6212_queue_slot_t queue_slots[AP6212_QUEUE_COUNT];

static uint32_t ap6212_enter_critical(void);
static void ap6212_exit_critical(uint32_t interrupt_state);
static ULONG ap6212_milliseconds_to_ticks(uint32_t milliseconds);
wwd_result_t host_rtos_create_thread_with_arg(host_thread_type_t *thread,
                                              void (*entry_function)(uint32_t),
                                              const char *name,
                                              void *stack,
                                              uint32_t stack_size,
                                              uint32_t priority,
                                              uint32_t argument);

wwd_result_t host_rtos_create_thread(host_thread_type_t *thread,
                                     void (*entry_function)(uint32_t),
                                     const char *name,
                                     void *stack,
                                     uint32_t stack_size,
                                     uint32_t priority)
{
    return host_rtos_create_thread_with_arg(thread,
                                            entry_function,
                                            name,
                                            stack,
                                            stack_size,
                                            priority,
                                            0U);
}

wwd_result_t host_rtos_create_thread_with_arg(host_thread_type_t *thread,
                                              void (*entry_function)(uint32_t),
                                              const char *name,
                                              void *stack,
                                              uint32_t stack_size,
                                              uint32_t priority,
                                              uint32_t argument)
{
    ap6212_thread_slot_t *slot = NULL;
    uint32_t interrupt_state;
    uint32_t index;
    UINT thread_priority;

    (void)stack;
    (void)stack_size;

    if ((thread == NULL) || (entry_function == NULL))
    {
        return WWD_THREAD_STACK_NULL;
    }

    interrupt_state = ap6212_enter_critical();
    for (index = 0U; index < AP6212_THREAD_COUNT; index++)
    {
        if (thread_slots[index].allocated == 0U)
        {
            thread_slots[index].allocated = 1U;
            slot = &thread_slots[index];
            break;
        }
    }
    ap6212_exit_critical(interrupt_state);

    if (slot == NULL)
    {
        return WWD_THREAD_STACK_NULL;
    }

    thread_priority = (priority < TX_MAX_PRIORITIES) ? (UINT)priority :
                                                     (TX_MAX_PRIORITIES - 1U);
    if (tx_thread_create(&slot->control_block,
                         (CHAR *)name,
                         (VOID (*)(ULONG))entry_function,
                         (ULONG)argument,
                         slot->stack,
                         sizeof(slot->stack),
                         thread_priority,
                         thread_priority,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START) != TX_SUCCESS)
    {
        slot->allocated = 0U;
        return 1031;
    }

    thread->object = slot;
    bsp_uart_write_string(BSP_UART_DEBUG, "WICED RTOS: worker thread started\r\n");
    return WWD_SUCCESS;
}

wwd_result_t host_rtos_finish_thread(host_thread_type_t *thread)
{
    ap6212_thread_slot_t *slot;

    if ((thread == NULL) || (thread->object == NULL))
    {
        return WWD_THREAD_FINISH_FAIL;
    }

    slot = (ap6212_thread_slot_t *)thread->object;
    return (tx_thread_terminate(&slot->control_block) == TX_SUCCESS) ?
        WWD_SUCCESS : WWD_THREAD_FINISH_FAIL;
}

wwd_result_t host_rtos_delete_terminated_thread(host_thread_type_t *thread)
{
    ap6212_thread_slot_t *slot;

    if ((thread == NULL) || (thread->object == NULL))
    {
        return WWD_THREAD_DELETE_FAIL;
    }

    slot = (ap6212_thread_slot_t *)thread->object;
    if (tx_thread_delete(&slot->control_block) != TX_SUCCESS)
    {
        return WWD_THREAD_DELETE_FAIL;
    }

    slot->allocated = 0U;
    thread->object = NULL;
    return WWD_SUCCESS;
}

wwd_result_t host_rtos_join_thread(host_thread_type_t *thread)
{
    ap6212_thread_slot_t *slot;

    if ((thread == NULL) || (thread->object == NULL))
    {
        return WWD_THREAD_DELETE_FAIL;
    }

    slot = (ap6212_thread_slot_t *)thread->object;
    while ((slot->control_block.tx_thread_state != TX_COMPLETED) &&
           (slot->control_block.tx_thread_state != TX_TERMINATED))
    {
        (void)tx_thread_sleep(1U);
    }

    return WWD_SUCCESS;
}

wwd_result_t host_rtos_init_semaphore(host_semaphore_type_t *semaphore)
{
    ap6212_semaphore_slot_t *slot = NULL;
    uint32_t interrupt_state;
    uint32_t index;

    if (semaphore == NULL)
    {
        return WWD_SEMAPHORE_ERROR;
    }

    interrupt_state = ap6212_enter_critical();
    for (index = 0U; index < AP6212_SEMAPHORE_COUNT; index++)
    {
        if (semaphore_slots[index].allocated == 0U)
        {
            semaphore_slots[index].allocated = 1U;
            slot = &semaphore_slots[index];
            break;
        }
    }
    ap6212_exit_critical(interrupt_state);

    if ((slot == NULL) ||
        (tx_semaphore_create(&slot->control_block, "wwd_sem", 0U) != TX_SUCCESS))
    {
        if (slot != NULL)
        {
            slot->allocated = 0U;
        }
        return WWD_SEMAPHORE_ERROR;
    }

    semaphore->object = slot;
    return WWD_SUCCESS;
}

wwd_result_t host_rtos_get_semaphore(host_semaphore_type_t *semaphore,
                                     uint32_t timeout_ms,
                                     int32_t will_set_in_isr)
{
    ap6212_semaphore_slot_t *slot;
    UINT result;

    (void)will_set_in_isr;

    if ((semaphore == NULL) || (semaphore->object == NULL))
    {
        return WWD_SEMAPHORE_ERROR;
    }

    slot = (ap6212_semaphore_slot_t *)semaphore->object;
    result = tx_semaphore_get(&slot->control_block,
                              (timeout_ms == AP6212_NEVER_TIMEOUT) ?
                                  TX_WAIT_FOREVER :
                                  ap6212_milliseconds_to_ticks(timeout_ms));
    if (result == TX_SUCCESS)
    {
        return WWD_SUCCESS;
    }
    if (result == TX_NO_INSTANCE)
    {
        return WWD_TIMEOUT;
    }
    if (result == TX_WAIT_ABORTED)
    {
        return WWD_WAIT_ABORTED;
    }
    return WWD_SEMAPHORE_ERROR;
}

wwd_result_t host_rtos_set_semaphore(host_semaphore_type_t *semaphore,
                                     int32_t called_from_isr)
{
    ap6212_semaphore_slot_t *slot;

    (void)called_from_isr;

    if ((semaphore == NULL) || (semaphore->object == NULL))
    {
        return WWD_SEMAPHORE_ERROR;
    }

    slot = (ap6212_semaphore_slot_t *)semaphore->object;
    return (tx_semaphore_put(&slot->control_block) == TX_SUCCESS) ?
        WWD_SUCCESS : WWD_SEMAPHORE_ERROR;
}

wwd_result_t host_rtos_deinit_semaphore(host_semaphore_type_t *semaphore)
{
    ap6212_semaphore_slot_t *slot;

    if ((semaphore == NULL) || (semaphore->object == NULL))
    {
        return WWD_SEMAPHORE_ERROR;
    }

    slot = (ap6212_semaphore_slot_t *)semaphore->object;
    if (tx_semaphore_delete(&slot->control_block) != TX_SUCCESS)
    {
        return WWD_SEMAPHORE_ERROR;
    }

    slot->allocated = 0U;
    semaphore->object = NULL;
    return WWD_SUCCESS;
}

uint32_t host_rtos_get_time(void)
{
    return (uint32_t)(((uint64_t)tx_time_get() * 1000U) /
                      TX_TIMER_TICKS_PER_SECOND);
}

wwd_result_t host_rtos_delay_milliseconds(uint32_t milliseconds)
{
    if (milliseconds != 0U)
    {
        if (tx_thread_sleep(ap6212_milliseconds_to_ticks(milliseconds)) != TX_SUCCESS)
        {
            return WWD_SLEEP_ERROR;
        }
    }
    return WWD_SUCCESS;
}

wwd_result_t host_rtos_init_queue(host_queue_type_t *queue,
                                  void *buffer,
                                  uint32_t buffer_size,
                                  uint32_t message_size)
{
    ap6212_queue_slot_t *slot = NULL;
    uint32_t interrupt_state;
    uint32_t index;

    if ((queue == NULL) || (buffer == NULL) ||
        (message_size == 0U) || ((message_size & 3U) != 0U))
    {
        return WWD_QUEUE_ERROR;
    }

    interrupt_state = ap6212_enter_critical();
    for (index = 0U; index < AP6212_QUEUE_COUNT; index++)
    {
        if (queue_slots[index].allocated == 0U)
        {
            queue_slots[index].allocated = 1U;
            slot = &queue_slots[index];
            break;
        }
    }
    ap6212_exit_critical(interrupt_state);

    if ((slot == NULL) ||
        (tx_queue_create(&slot->control_block,
                         "wwd_queue",
                         message_size / sizeof(ULONG),
                         buffer,
                         buffer_size) != TX_SUCCESS))
    {
        if (slot != NULL)
        {
            slot->allocated = 0U;
        }
        return WWD_QUEUE_ERROR;
    }

    queue->object = slot;
    queue->message_length = (int32_t)message_size;
    return WWD_SUCCESS;
}

wwd_result_t host_rtos_push_to_queue(host_queue_type_t *queue,
                                     void *message,
                                     uint32_t timeout_ms)
{
    ap6212_queue_slot_t *slot;

    if ((queue == NULL) || (queue->object == NULL))
    {
        return WWD_QUEUE_ERROR;
    }

    slot = (ap6212_queue_slot_t *)queue->object;
    return (tx_queue_send(&slot->control_block,
                          message,
                          (timeout_ms == AP6212_NEVER_TIMEOUT) ?
                              TX_WAIT_FOREVER :
                              ap6212_milliseconds_to_ticks(timeout_ms)) == TX_SUCCESS) ?
        WWD_SUCCESS : WWD_QUEUE_ERROR;
}

wwd_result_t host_rtos_pop_from_queue(host_queue_type_t *queue,
                                      void *message,
                                      uint32_t timeout_ms)
{
    ap6212_queue_slot_t *slot;
    UINT result;

    if ((queue == NULL) || (queue->object == NULL))
    {
        return WWD_QUEUE_ERROR;
    }

    slot = (ap6212_queue_slot_t *)queue->object;
    result = tx_queue_receive(&slot->control_block,
                              message,
                              (timeout_ms == AP6212_NEVER_TIMEOUT) ?
                                  TX_WAIT_FOREVER :
                                  ap6212_milliseconds_to_ticks(timeout_ms));
    if (result == TX_SUCCESS)
    {
        return WWD_SUCCESS;
    }
    if (result == TX_QUEUE_EMPTY)
    {
        return WWD_TIMEOUT;
    }
    if (result == TX_WAIT_ABORTED)
    {
        return WWD_WAIT_ABORTED;
    }
    return WWD_QUEUE_ERROR;
}

wwd_result_t host_rtos_deinit_queue(host_queue_type_t *queue)
{
    ap6212_queue_slot_t *slot;

    if ((queue == NULL) || (queue->object == NULL))
    {
        return WWD_QUEUE_ERROR;
    }

    slot = (ap6212_queue_slot_t *)queue->object;
    if (tx_queue_delete(&slot->control_block) != TX_SUCCESS)
    {
        return WWD_QUEUE_ERROR;
    }

    slot->allocated = 0U;
    queue->object = NULL;
    queue->message_length = 0;
    return WWD_SUCCESS;
}

static uint32_t ap6212_enter_critical(void)
{
    uint32_t interrupt_state = __get_PRIMASK();
    __disable_irq();
    return interrupt_state;
}

static void ap6212_exit_critical(uint32_t interrupt_state)
{
    __set_PRIMASK(interrupt_state);
}

static ULONG ap6212_milliseconds_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks = ((uint64_t)milliseconds * TX_TIMER_TICKS_PER_SECOND + 999U) /
                     1000U;

    if ((milliseconds != 0U) && (ticks == 0U))
    {
        ticks = 1U;
    }
    if (ticks >= TX_WAIT_FOREVER)
    {
        ticks = TX_WAIT_FOREVER - 1U;
    }
    return (ULONG)ticks;
}
