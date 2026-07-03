#include "ldc_uart4_test.h"

#include <stdbool.h>
#include <string.h>

#include "ldc_easy.h"
#include "main.h"
#include "ldc_port_irq.h"
#include "usart.h"

#ifndef LDC_UART4_TEST_USE_DMA
#define LDC_UART4_TEST_USE_DMA 0
#endif

#define LDC_UART4_TEST_MAX_FRAME 256U
#define LDC_UART4_TEST_PACKETS   4U
#define LDC_UART4_TEST_IDLE_MS   30U
#define LDC_UART4_TEST_RX_SIZE   256U
#define LDC_UART4_TEST_TX_TIMEOUT_MS 100U

static ldc_easy_t s_ldc;
static uint8_t s_ldc_ring[LDC_EASY_RING_BYTES(LDC_UART4_TEST_MAX_FRAME,
                                              LDC_UART4_TEST_PACKETS)];
static ldc_packet_t s_ldc_packets[LDC_UART4_TEST_PACKETS];
static uint8_t s_frame[LDC_UART4_TEST_MAX_FRAME];
static volatile uint32_t s_packet_events;
static volatile uint32_t s_overflow_events;
static volatile uint32_t s_drop_events;
static ldc_uart4_test_packet_handler_t s_packet_handler;
static void *s_packet_handler_arg;
static ldc_uart4_test_event_handler_t s_event_handler;
static void *s_event_handler_arg;

#if LDC_UART4_TEST_USE_DMA
static uint8_t s_dma_rx[LDC_UART4_TEST_RX_SIZE];
static uint16_t s_dma_last_pos;
#else
static uint8_t s_rx_byte;
#endif

static void ldc_uart4_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_EASY_EVT_PACKET)
        s_packet_events++;
    else if(event == LDC_EASY_EVT_OVERFLOW)
        s_overflow_events++;
    else
        s_drop_events++;

    if(s_event_handler != NULL)
        s_event_handler(event, s_event_handler_arg);
}

void ldc_uart4_test_write(const uint8_t *data, uint16_t len)
{
    if(data != NULL && len != 0U)
        (void)HAL_UART_Transmit(&huart4, (uint8_t *)data, len, LDC_UART4_TEST_TX_TIMEOUT_MS);
}

void ldc_uart4_test_write_text(const char *text)
{
    if(text != NULL)
        ldc_uart4_test_write((const uint8_t *)text, (uint16_t)strlen(text));
}

void ldc_uart4_test_write_u32(uint32_t value)
{
    char buf[11];
    uint32_t pos = sizeof(buf);

    buf[--pos] = '\0';
    do
    {
        buf[--pos] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(value != 0U && pos != 0U);

    ldc_uart4_test_write_text(&buf[pos]);
}

static void ldc_uart4_default_packet_handler(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;

    ldc_uart4_test_write_text("LDC RX ");
    ldc_uart4_test_write_u32((uint32_t)len);
    ldc_uart4_test_write_text(" bytes: ");
    ldc_uart4_test_write(data, len);
    if(len < 2U || data[len - 1U] != '\n')
        ldc_uart4_test_write_text("\r\n");
}

static void uart4_send_banner(void)
{
#if LDC_UART4_TEST_USE_DMA
    ldc_uart4_test_write_text("\r\nART-Pi LDC UART4 DMA test ready\r\n");
#else
    ldc_uart4_test_write_text("\r\nART-Pi LDC UART4 byte-IT test ready\r\n");
#endif
    ldc_uart4_test_write_text("115200 8N1. Send text; newline or idle gap makes one LDC packet.\r\n");
}

#if LDC_UART4_TEST_USE_DMA
static HAL_StatusTypeDef uart4_start_rx(void)
{
    HAL_StatusTypeDef status;

    s_dma_last_pos = 0U;
    status = HAL_UARTEx_ReceiveToIdle_DMA(&huart4, s_dma_rx, (uint16_t)sizeof(s_dma_rx));
    if(status == HAL_OK && huart4.hdmarx != NULL)
        __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);

    return status;
}

static bool uart4_dma_is_circular(void)
{
    return huart4.hdmarx != NULL &&
           huart4.hdmarx->Init.Mode == DMA_CIRCULAR;
}

static void uart4_dma_add_range(uint16_t start, uint16_t end)
{
    if(end > start)
        (void)ldc_easy_add(&s_ldc, &s_dma_rx[start], (uint32_t)(end - start));
}

static void uart4_dma_rx_event(uint16_t pos, bool commit)
{
    const uint16_t size = (uint16_t)sizeof(s_dma_rx);

    if(pos > size)
        pos = size;

    if(uart4_dma_is_circular())
    {
        if(pos != s_dma_last_pos)
        {
            if(pos > s_dma_last_pos)
            {
                uart4_dma_add_range(s_dma_last_pos, pos);
            }
            else
            {
                uart4_dma_add_range(s_dma_last_pos, size);
                uart4_dma_add_range(0U, pos);
            }
            s_dma_last_pos = pos;
        }

        if(commit)
            (void)ldc_easy_settle(&s_ldc);
    }
    else
    {
        if(pos != 0U)
            (void)ldc_easy_rx_idle(&s_ldc, s_dma_rx, pos);
        (void)uart4_start_rx();
    }
}
#else
static HAL_StatusTypeDef uart4_start_rx(void)
{
    return HAL_UART_Receive_IT(&huart4, &s_rx_byte, 1U);
}
#endif

void ldc_uart4_test_set_packet_handler(ldc_uart4_test_packet_handler_t handler,
                                       void *arg)
{
    s_packet_handler = handler;
    s_packet_handler_arg = arg;
}

void ldc_uart4_test_set_event_handler(ldc_uart4_test_event_handler_t handler,
                                      void *arg)
{
    s_event_handler = handler;
    s_event_handler_arg = arg;
}

void ldc_uart4_test_init(void)
{
    ldc_easy_config_t config;

    memset(&config, 0, sizeof(config));
    config.ring_buffer = s_ldc_ring;
    config.ring_size = (uint32_t)sizeof(s_ldc_ring);
    config.packet_pool = s_ldc_packets;
    config.packet_count = LDC_UART4_TEST_PACKETS;
    config.max_frame = LDC_UART4_TEST_MAX_FRAME;
    config.timeout_ms = LDC_UART4_TEST_IDLE_MS;
    config.delimiter_enabled = true;
    config.delimiter = (uint8_t)'\n';
    config.mode = LDC_MODE_PROTECT;
    config.lock = ldc_port_irq_lock;
    config.unlock = ldc_port_irq_unlock;
    config.event_cb = ldc_uart4_event;
    config.auto_tick = true;

    if(!ldc_easy_init(&s_ldc, &config))
        Error_Handler();

    uart4_send_banner();

    if(uart4_start_rx() != HAL_OK)
        Error_Handler();
}

void ldc_uart4_test_tick_ms(uint32_t elapsed_ms)
{
    if(elapsed_ms != 0U)
        ldc_easy_tick(&s_ldc, elapsed_ms);
}

void ldc_uart4_test_poll(void)
{
    int len;
    ldc_uart4_test_packet_handler_t handler;

    while((len = ldc_easy_pop(&s_ldc, s_frame, (uint32_t)sizeof(s_frame))) > 0)
    {
        handler = s_packet_handler;
        if(handler != NULL)
            handler(s_frame, (uint16_t)len, s_packet_handler_arg);
        else
            ldc_uart4_default_packet_handler(s_frame, (uint16_t)len, NULL);
    }

    if(len < 0)
    {
        (void)ldc_easy_abort(&s_ldc);
        ldc_uart4_test_write_text("LDC read error; frame dropped\r\n");
    }
}

#if LDC_UART4_TEST_USE_DMA
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    HAL_UART_RxEventTypeTypeDef event;
    bool commit;

    if(huart == NULL || huart->Instance != UART4)
        return;

    event = HAL_UARTEx_GetRxEventType(huart);
    commit = uart4_dma_is_circular() ? (event == HAL_UART_RXEVENT_IDLE) : true;
    uart4_dma_rx_event(Size, commit);
}
#else
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart == NULL || huart->Instance != UART4)
        return;

    (void)ldc_easy_putc(&s_ldc, s_rx_byte);
    (void)uart4_start_rx();
}
#endif

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart == NULL || huart->Instance != UART4)
        return;

    (void)ldc_easy_abort(&s_ldc);
    (void)uart4_start_rx();
}
