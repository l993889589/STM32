#include "ap6212_wiced.h"
#include "bsp.h"
#include "stm32h7xx.h"
#include "tx_api.h"

#include <stddef.h>
#include <stdint.h>

#define AP6212_BUFFER_COUNT     24U
#define AP6212_BUFFER_CAPACITY 2048U
#define AP6212_BUFFER_HEADROOM  128U

typedef struct
{
    uint16_t data_offset;
    uint16_t data_size;
    uint8_t allocated;
    uint8_t reserved[3];
    uint8_t data[AP6212_BUFFER_CAPACITY];
} ap6212_buffer_t;

__attribute__((aligned(32)))
static ap6212_buffer_t buffer_pool[AP6212_BUFFER_COUNT];

wwd_result_t host_buffer_init(void *native_argument)
{
    uint32_t index;

    (void)native_argument;
    bsp_uart_write_string(BSP_UART_DEBUG, "WICED buffer_init: begin\r\n");
    for (index = 0U; index < AP6212_BUFFER_COUNT; index++)
    {
        buffer_pool[index].allocated = 0U;
    }
    bsp_uart_write_string(BSP_UART_DEBUG, "WICED buffer_init: ready\r\n");
    return WWD_SUCCESS;
}

wwd_result_t host_buffer_get(wiced_buffer_t *buffer,
                             int32_t direction,
                             uint16_t size,
                             int32_t wait)
{
    uint32_t interrupt_state;
    uint32_t index;
    uint32_t attempts = (wait != WICED_FALSE) ? 100U : 1U;

    (void)direction;

    if ((buffer == NULL) || (size > AP6212_BUFFER_CAPACITY))
    {
        return WWD_BUFFER_ALLOC_FAIL;
    }

    while (attempts-- != 0U)
    {
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        for (index = 0U; index < AP6212_BUFFER_COUNT; index++)
        {
            if (buffer_pool[index].allocated == 0U)
            {
                buffer_pool[index].allocated = 1U;
                buffer_pool[index].data_offset =
                    (size <= (AP6212_BUFFER_CAPACITY - AP6212_BUFFER_HEADROOM)) ?
                        AP6212_BUFFER_HEADROOM : 0U;
                buffer_pool[index].data_size = size;
                *buffer = &buffer_pool[index];
                __set_PRIMASK(interrupt_state);
                return WWD_SUCCESS;
            }
        }
        __set_PRIMASK(interrupt_state);

        if ((wait != WICED_FALSE) && (tx_thread_identify() != TX_NULL))
        {
            (void)tx_thread_sleep(1U);
        }
    }

    return WWD_BUFFER_ALLOC_FAIL;
}

void host_buffer_release(wiced_buffer_t buffer, int32_t direction)
{
    ap6212_buffer_t *packet = (ap6212_buffer_t *)buffer;
    uint32_t interrupt_state;

    (void)direction;
    if (packet == NULL)
    {
        return;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    packet->allocated = 0U;
    packet->data_offset = 0U;
    packet->data_size = 0U;
    __set_PRIMASK(interrupt_state);
}

uint8_t *host_buffer_get_current_piece_data_pointer(wiced_buffer_t buffer)
{
    ap6212_buffer_t *packet = (ap6212_buffer_t *)buffer;

    return (packet != NULL) ? &packet->data[packet->data_offset] : NULL;
}

uint16_t host_buffer_get_current_piece_size(wiced_buffer_t buffer)
{
    ap6212_buffer_t *packet = (ap6212_buffer_t *)buffer;

    return (packet != NULL) ? packet->data_size : 0U;
}

wwd_result_t host_buffer_set_size(wiced_buffer_t buffer, uint16_t size)
{
    ap6212_buffer_t *packet = (ap6212_buffer_t *)buffer;

    if ((packet == NULL) ||
        (((uint32_t)packet->data_offset + size) > AP6212_BUFFER_CAPACITY))
    {
        return WWD_BUFFER_SIZE_SET_ERROR;
    }

    packet->data_size = size;
    return WWD_SUCCESS;
}

wiced_buffer_t host_buffer_get_next_piece(wiced_buffer_t buffer)
{
    (void)buffer;
    return NULL;
}

wwd_result_t host_buffer_add_remove_at_front(wiced_buffer_t *buffer,
                                             int32_t add_remove_amount)
{
    ap6212_buffer_t *packet;
    int32_t new_offset;
    int32_t new_size;

    if ((buffer == NULL) || (*buffer == NULL))
    {
        return WWD_BUFFER_POINTER_MOVE_ERROR;
    }

    packet = (ap6212_buffer_t *)*buffer;
    new_offset = (int32_t)packet->data_offset + add_remove_amount;
    new_size = (int32_t)packet->data_size - add_remove_amount;
    if ((new_offset < 0) || (new_size < 0) ||
        (((uint32_t)new_offset + (uint32_t)new_size) > AP6212_BUFFER_CAPACITY))
    {
        return WWD_BUFFER_POINTER_MOVE_ERROR;
    }

    packet->data_offset = (uint16_t)new_offset;
    packet->data_size = (uint16_t)new_size;
    return WWD_SUCCESS;
}

wwd_result_t host_buffer_check_leaked(void)
{
    uint32_t index;

    for (index = 0U; index < AP6212_BUFFER_COUNT; index++)
    {
        if (buffer_pool[index].allocated != 0U)
        {
            return WWD_BUFFER_ALLOC_FAIL;
        }
    }
    return WWD_SUCCESS;
}

void host_network_process_ethernet_data(wiced_buffer_t buffer, int32_t interface)
{
    (void)interface;
    host_buffer_release(buffer, 1);
}
