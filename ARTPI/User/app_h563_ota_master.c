#include "app_h563_ota_master.h"

#include <stdio.h>
#include <string.h>

#include "app_h563_ota_cache.h"
#include "bsp.h"
#include "ld_modbus.h"
#include "ota_modbus_protocol.h"
#include "tx_api.h"

#define APP_H563_OTA_BASE_BAUD_RATE          115200UL
#define APP_H563_OTA_SWITCH_DELAY_MS             50U
#define APP_H563_OTA_BAUD_LEASE_MS             800U
#define APP_H563_OTA_RESPONSE_TIMEOUT_MS        500U
/* External-slot erase and full signed-image verification are intentionally
 * blocking on the H563 service side, so their timeouts must cover worst-case
 * NOR erase and software ECDSA/SHA execution rather than an RTU round trip. */
#define APP_H563_OTA_BEGIN_TIMEOUT_MS        120000U
#define APP_H563_OTA_FINISH_TIMEOUT_MS        60000U
#define APP_H563_OTA_MAX_DATA_FAILURES            5U
#define APP_H563_OTA_SYNC_PASSES                  3U
#define APP_H563_OTA_REBOOT_DELAY_MS             100U

typedef enum
{
    APP_H563_OTA_EXCHANGE_OK = 0,
    APP_H563_OTA_EXCHANGE_TIMEOUT,
    APP_H563_OTA_EXCHANGE_PROTOCOL,
    APP_H563_OTA_EXCHANGE_REMOTE
} app_h563_ota_exchange_result_t;

static TX_MUTEX app_h563_ota_master_mutex;
static ld_modbus_rtu_framer_t *app_h563_ota_receiver;
static uint8_t app_h563_ota_bits_per_char;
static uint32_t app_h563_ota_timestamp_hz;
static app_h563_ota_master_status_t app_h563_ota_master_status;
static uint8_t app_h563_ota_master_initialized;
static uint8_t app_h563_ota_master_pending;
static uint8_t app_h563_ota_master_running;

/** @brief Replace RTU timing while the BSP keeps the UART IRQ masked. */
static void app_h563_ota_sync_receiver_baud(uint32_t active_baud_rate,
                                             void *argument)
{
    uint32_t interrupt_state = __get_PRIMASK();

    (void)argument;
    if(interrupt_state == 0U)
    {
        __disable_irq();
        __DMB();
    }
    (void)ld_modbus_rtu_framer_reconfigure(app_h563_ota_receiver,
                                            active_baud_rate,
                                            app_h563_ota_bits_per_char,
                                            app_h563_ota_timestamp_hz);
    if(interrupt_state == 0U)
    {
        __DMB();
        __enable_irq();
    }
}

static uint32_t app_h563_ota_now_ms(void)
{
    return (uint32_t)(((uint64_t)tx_time_get() * 1000ULL) /
                      TX_TIMER_TICKS_PER_SECOND);
}

static ULONG app_h563_ota_ms_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks = ((uint64_t)milliseconds * TX_TIMER_TICKS_PER_SECOND +
                      999ULL) / 1000ULL;

    return (ticks == 0ULL) ? 1UL : (ULONG)ticks;
}

static uint8_t app_h563_ota_time_reached(ULONG now, ULONG deadline)
{
    return ((LONG)(now - deadline) >= 0L) ? 1U : 0U;
}

static void app_h563_ota_set_phase(uint8_t phase)
{
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_status.phase = phase;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
}

static void app_h563_ota_set_progress(uint32_t transferred_size)
{
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_status.transferred_size = transferred_size;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
}

static void app_h563_ota_set_remote_status(uint8_t remote_status)
{
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_status.last_remote_status = remote_status;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
}

static void app_h563_ota_count_retry(void)
{
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_status.retry_count++;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
}

static void app_h563_ota_clear_receive_queue(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    if(interrupt_state == 0U)
    {
        __disable_irq();
        __DMB();
    }
    (void)ld_modbus_rtu_framer_reset(app_h563_ota_receiver);
    if(interrupt_state == 0U)
    {
        __DMB();
        __enable_irq();
    }
}

static void app_h563_ota_poll_receive_queue(void)
{
    uint32_t interrupt_state = __get_PRIMASK();

    if(interrupt_state == 0U)
    {
        __disable_irq();
        __DMB();
    }
    if(bsp_rs485_receive_quiescent() != 0U)
    {
        ld_modbus_rtu_framer_poll(app_h563_ota_receiver,
                                  bsp_dwt_get_cycles());
    }
    if(interrupt_state == 0U)
    {
        __DMB();
        __enable_irq();
    }
}

static HAL_StatusTypeDef app_h563_ota_set_baud_rate(uint32_t baud_rate)
{
    ld_modbus_rtu_timing_t validated_timing;

    if(!ld_modbus_rtu_timing_init(&validated_timing,
                                  baud_rate,
                                  app_h563_ota_bits_per_char,
                                  app_h563_ota_timestamp_hz))
    {
        return HAL_ERROR;
    }
    return bsp_uart_set_baud_rate_synchronized(
        BSP_RS485_UART_PORT,
        baud_rate,
        app_h563_ota_sync_receiver_baud,
        NULL);
}

/** @brief Claim ready receiver metadata under a short UART critical section. */
static uint8_t app_h563_ota_claim_frame(ld_modbus_rtu_frame_view_t *view)
{
    uint32_t interrupt_state = __get_PRIMASK();
    uint8_t claimed;

    if(interrupt_state == 0U)
    {
        __disable_irq();
        __DMB();
    }
    claimed = ld_modbus_rtu_framer_claim(app_h563_ota_receiver, view) ?
              1U : 0U;
    if(interrupt_state == 0U)
    {
        __DMB();
        __enable_irq();
    }
    return claimed;
}

/** @brief Release a claimed receiver frame under the same short lock. */
static void app_h563_ota_release_frame(ld_modbus_rtu_frame_view_t *view)
{
    uint32_t interrupt_state = __get_PRIMASK();

    if(interrupt_state == 0U)
    {
        __disable_irq();
        __DMB();
    }
    (void)ld_modbus_rtu_framer_release(app_h563_ota_receiver, view);
    if(interrupt_state == 0U)
    {
        __DMB();
        __enable_irq();
    }
}

static app_h563_ota_exchange_result_t app_h563_ota_exchange(
    uint8_t unit_id,
    const ota_modbus_request_t *request,
    ota_modbus_response_t *response,
    uint32_t timeout_ms)
{
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    uint8_t frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    size_t pdu_length;
    size_t frame_length;
    ULONG deadline;
    if(ota_modbus_encode_request(request,
                                  pdu,
                                  sizeof(pdu),
                                  &pdu_length) != OTA_MODBUS_CODEC_OK ||
       ld_modbus_rtu_encode(unit_id,
                            pdu,
                            pdu_length,
                            frame,
                            sizeof(frame),
                            &frame_length) != LD_MODBUS_STATUS_OK)
    {
        return APP_H563_OTA_EXCHANGE_PROTOCOL;
    }

    app_h563_ota_clear_receive_queue();
    if(bsp_rs485_write(frame, frame_length) != frame_length)
        return APP_H563_OTA_EXCHANGE_TIMEOUT;

    deadline = tx_time_get() + app_h563_ota_ms_to_ticks(timeout_ms);
    while(bsp_rs485_tx_empty() == 0U)
    {
        if(app_h563_ota_time_reached(tx_time_get(), deadline) != 0U)
            return APP_H563_OTA_EXCHANGE_TIMEOUT;
        (void)tx_thread_sleep(1U);
    }

    while(app_h563_ota_time_reached(tx_time_get(), deadline) == 0U)
    {
        ld_modbus_rtu_frame_view_t received_frame;

        app_h563_ota_poll_receive_queue();
        if(app_h563_ota_claim_frame(&received_frame) != 0U)
        {
            ld_modbus_adu_view_t view;
            uint8_t matched = 0U;

            if(ld_modbus_rtu_decode(received_frame.data,
                                    received_frame.length,
                                    &view) == LD_MODBUS_STATUS_OK &&
               view.unit_id == unit_id &&
               ota_modbus_decode_response(view.pdu,
                                           view.pdu_length,
                                           response) == OTA_MODBUS_CODEC_OK &&
               response->command == request->command &&
               response->session_id == request->session_id)
            {
                matched = 1U;
            }
            app_h563_ota_release_frame(&received_frame);
            if(matched != 0U)
            {
                if(response->status != OTA_MODBUS_STATUS_OK)
                {
                    app_h563_ota_set_remote_status(response->status);
                    return APP_H563_OTA_EXCHANGE_REMOTE;
                }
                return APP_H563_OTA_EXCHANGE_OK;
            }
        }
        (void)tx_thread_sleep(1U);
    }
    return APP_H563_OTA_EXCHANGE_TIMEOUT;
}

static uint8_t app_h563_ota_baud_is_supported(
    const ota_modbus_response_t *hello,
    uint32_t baud_rate)
{
    uint32_t index;

    for(index = 0U; index < hello->baud_rate_count; index++)
    {
        if(hello->baud_rates[index] == baud_rate)
            return 1U;
    }
    return 0U;
}

static uint32_t app_h563_ota_select_baud(
    const ota_modbus_response_t *hello,
    uint32_t preferred_baud_rate)
{
    uint32_t selected = APP_H563_OTA_BASE_BAUD_RATE;
    uint32_t limit = (preferred_baud_rate == 0U) ? UINT32_MAX :
                     preferred_baud_rate;
    uint32_t index;

    for(index = 0U; index < hello->baud_rate_count; index++)
    {
        uint32_t baud_rate = hello->baud_rates[index];

        if(baud_rate <= limit && baud_rate > selected)
            selected = baud_rate;
    }
    return selected;
}

static uint8_t app_h563_ota_change_baud(uint8_t unit_id,
                                         uint32_t session_id,
                                         uint32_t baud_rate)
{
    ota_modbus_request_t request;
    ota_modbus_response_t response;
    uint32_t pass;

    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_SET_BAUD;
    request.session_id = session_id;
    request.baud_rate = baud_rate;
    request.switch_delay_ms = APP_H563_OTA_SWITCH_DELAY_MS;
    request.fallback_timeout_ms = APP_H563_OTA_BAUD_LEASE_MS;
    if(app_h563_ota_exchange(unit_id,
                              &request,
                              &response,
                              APP_H563_OTA_RESPONSE_TIMEOUT_MS) !=
       APP_H563_OTA_EXCHANGE_OK)
    {
        return 0U;
    }

    (void)tx_thread_sleep(app_h563_ota_ms_to_ticks(
        (uint32_t)response.switch_delay_ms + 5U));
    app_h563_ota_clear_receive_queue();
    if(app_h563_ota_set_baud_rate(baud_rate) != HAL_OK)
        return 0U;

    for(pass = 0U; pass < APP_H563_OTA_SYNC_PASSES; pass++)
    {
        memset(&request, 0, sizeof(request));
        request.command = OTA_MODBUS_COMMAND_SYNC;
        request.session_id = session_id;
        request.baud_rate = baud_rate;
        if(app_h563_ota_exchange(unit_id,
                                  &request,
                                  &response,
                                  APP_H563_OTA_RESPONSE_TIMEOUT_MS) !=
               APP_H563_OTA_EXCHANGE_OK ||
           response.baud_rate != baud_rate)
        {
            return 0U;
        }
    }
    return 1U;
}

static app_h563_ota_exchange_result_t app_h563_ota_get_remote_status(
    uint8_t unit_id,
    uint32_t session_id,
    ota_modbus_response_t *response)
{
    ota_modbus_request_t request;

    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_STATUS;
    request.session_id = session_id;
    return app_h563_ota_exchange(unit_id,
                                  &request,
                                  response,
                                  APP_H563_OTA_RESPONSE_TIMEOUT_MS);
}

static uint8_t app_h563_ota_transfer_image(uint8_t unit_id,
                                            uint32_t session_id,
                                            uint32_t offset,
                                            uint32_t image_size)
{
    uint8_t data[OTA_MODBUS_MAX_DATA_SIZE];
    uint32_t consecutive_failures = 0U;

    while(offset < image_size)
    {
        ota_modbus_request_t request;
        ota_modbus_response_t response;
        app_h563_ota_exchange_result_t result;
        uint32_t remaining = image_size - offset;
        uint16_t length = (remaining > OTA_MODBUS_MAX_DATA_SIZE) ?
                          OTA_MODBUS_MAX_DATA_SIZE : (uint16_t)remaining;

        if(app_h563_ota_cache_read_image(offset, data, length) != HAL_OK)
            return 0U;

        memset(&request, 0, sizeof(request));
        request.command = OTA_MODBUS_COMMAND_DATA;
        request.session_id = session_id;
        request.offset = offset;
        request.data = data;
        request.data_length = length;
        result = app_h563_ota_exchange(unit_id,
                                        &request,
                                        &response,
                                        APP_H563_OTA_RESPONSE_TIMEOUT_MS);
        if(result == APP_H563_OTA_EXCHANGE_OK &&
           response.received_size > offset &&
           response.received_size <= image_size)
        {
            offset = response.received_size;
            consecutive_failures = 0U;
            app_h563_ota_set_progress(offset);
            continue;
        }

        consecutive_failures++;
        app_h563_ota_count_retry();
        if(consecutive_failures > APP_H563_OTA_MAX_DATA_FAILURES ||
           app_h563_ota_get_remote_status(unit_id,
                                           session_id,
                                           &response) !=
               APP_H563_OTA_EXCHANGE_OK ||
           response.expected_size != image_size ||
           response.received_size > image_size)
        {
            return 0U;
        }
        offset = response.received_size;
        app_h563_ota_set_progress(offset);
    }
    return 1U;
}

static uint8_t app_h563_ota_execute(uint8_t unit_id,
                                     uint32_t preferred_baud_rate,
                                     uint32_t session_id)
{
    ota_modbus_descriptor_t descriptor;
    ota_modbus_request_t request;
    ota_modbus_response_t response;
    app_h563_ota_exchange_result_t exchange_result;
    uint32_t baud_rate;

    if(app_h563_ota_cache_get_descriptor(&descriptor) != HAL_OK)
        return APP_H563_OTA_RESULT_CACHE_NOT_READY;

    if(bsp_uart_get_baud_rate(BSP_RS485_UART_PORT) !=
       APP_H563_OTA_BASE_BAUD_RATE)
    {
        if(app_h563_ota_set_baud_rate(APP_H563_OTA_BASE_BAUD_RATE) != HAL_OK)
        {
            return APP_H563_OTA_RESULT_BAUD;
        }
    }
    app_h563_ota_clear_receive_queue();

    app_h563_ota_set_phase(APP_H563_OTA_PHASE_HELLO);
    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_HELLO;
    request.session_id = session_id;
    exchange_result = app_h563_ota_exchange(unit_id,
                                             &request,
                                             &response,
                                             APP_H563_OTA_RESPONSE_TIMEOUT_MS);
    if(exchange_result != APP_H563_OTA_EXCHANGE_OK)
        return (exchange_result == APP_H563_OTA_EXCHANGE_REMOTE) ?
               APP_H563_OTA_RESULT_REMOTE : APP_H563_OTA_RESULT_TRANSPORT;
    if((response.capability_flags & (OTA_MODBUS_CAP_RESUME |
                                     OTA_MODBUS_CAP_SIGNED_IMAGE |
                                     OTA_MODBUS_CAP_AB_SLOTS)) !=
                                    (OTA_MODBUS_CAP_RESUME |
                                     OTA_MODBUS_CAP_SIGNED_IMAGE |
                                     OTA_MODBUS_CAP_AB_SLOTS) ||
       response.max_data_size < OTA_MODBUS_MAX_DATA_SIZE ||
       app_h563_ota_baud_is_supported(&response,
                                      APP_H563_OTA_BASE_BAUD_RATE) == 0U)
    {
        return APP_H563_OTA_RESULT_PROTOCOL;
    }

    baud_rate = app_h563_ota_select_baud(&response, preferred_baud_rate);
    if(baud_rate != APP_H563_OTA_BASE_BAUD_RATE)
    {
        app_h563_ota_set_phase(APP_H563_OTA_PHASE_BAUD_TEST);
        if(app_h563_ota_change_baud(unit_id, session_id, baud_rate) == 0U)
            return APP_H563_OTA_RESULT_BAUD;
    }
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_status.active_baud_rate = baud_rate;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }

    app_h563_ota_set_phase(APP_H563_OTA_PHASE_BEGIN);
    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_BEGIN;
    request.session_id = session_id;
    request.descriptor = descriptor;
    exchange_result = app_h563_ota_exchange(unit_id,
                                             &request,
                                             &response,
                                             APP_H563_OTA_BEGIN_TIMEOUT_MS);
    if(exchange_result != APP_H563_OTA_EXCHANGE_OK)
        return (exchange_result == APP_H563_OTA_EXCHANGE_REMOTE) ?
               APP_H563_OTA_RESULT_REMOTE : APP_H563_OTA_RESULT_TRANSPORT;
    if(response.expected_size != descriptor.image_size ||
       response.received_size > descriptor.image_size)
    {
        return APP_H563_OTA_RESULT_PROTOCOL;
    }

    app_h563_ota_set_phase(APP_H563_OTA_PHASE_TRANSFER);
    app_h563_ota_set_progress(response.received_size);
    if(app_h563_ota_transfer_image(unit_id,
                                    session_id,
                                    response.received_size,
                                    descriptor.image_size) == 0U)
    {
        return APP_H563_OTA_RESULT_TRANSPORT;
    }

    app_h563_ota_set_phase(APP_H563_OTA_PHASE_VERIFY);
    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_FINISH;
    request.session_id = session_id;
    exchange_result = app_h563_ota_exchange(unit_id,
                                             &request,
                                             &response,
                                             APP_H563_OTA_FINISH_TIMEOUT_MS);
    if(exchange_result != APP_H563_OTA_EXCHANGE_OK)
        return (exchange_result == APP_H563_OTA_EXCHANGE_REMOTE) ?
               APP_H563_OTA_RESULT_REMOTE : APP_H563_OTA_RESULT_TRANSPORT;
    if(response.received_size != descriptor.image_size ||
       response.expected_size != descriptor.image_size)
    {
        return APP_H563_OTA_RESULT_PROTOCOL;
    }

    app_h563_ota_set_phase(APP_H563_OTA_PHASE_ACTIVATE);
    memset(&request, 0, sizeof(request));
    request.command = OTA_MODBUS_COMMAND_ACTIVATE;
    request.session_id = session_id;
    request.reboot_delay_ms = APP_H563_OTA_REBOOT_DELAY_MS;
    exchange_result = app_h563_ota_exchange(unit_id,
                                             &request,
                                             &response,
                                             APP_H563_OTA_RESPONSE_TIMEOUT_MS);
    if(exchange_result != APP_H563_OTA_EXCHANGE_OK)
        return (exchange_result == APP_H563_OTA_EXCHANGE_REMOTE) ?
               APP_H563_OTA_RESULT_REMOTE : APP_H563_OTA_RESULT_TRANSPORT;
    return APP_H563_OTA_RESULT_OK;
}

HAL_StatusTypeDef app_h563_ota_master_init(
    ld_modbus_rtu_framer_t *receiver,
    uint8_t bits_per_char,
    uint32_t timestamp_hz)
{
    if(app_h563_ota_master_initialized != 0U)
        return HAL_OK;
    if(receiver == NULL || bits_per_char == 0U ||
       timestamp_hz == 0U ||
       tx_mutex_create(&app_h563_ota_master_mutex,
                       "h563_ota_master",
                       TX_INHERIT) != TX_SUCCESS ||
       app_h563_ota_cache_init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    memset(&app_h563_ota_master_status, 0,
           sizeof(app_h563_ota_master_status));
    app_h563_ota_receiver = receiver;
    app_h563_ota_bits_per_char = bits_per_char;
    app_h563_ota_timestamp_hz = timestamp_hz;
    app_h563_ota_master_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef app_h563_ota_master_request(uint8_t unit_id,
                                              uint32_t preferred_baud_rate)
{
    app_h563_ota_cache_status_t cache_status;

    if(app_h563_ota_master_initialized == 0U || unit_id == 0U ||
       unit_id > 247U)
    {
        return HAL_ERROR;
    }
    app_h563_ota_cache_get_status(&cache_status);
    if(cache_status.state != APP_H563_OTA_CACHE_READY)
        return HAL_ERROR;
    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return HAL_ERROR;
    if(app_h563_ota_master_pending != 0U ||
       app_h563_ota_master_running != 0U)
    {
        app_h563_ota_master_status.result = APP_H563_OTA_RESULT_BUSY;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
        return HAL_BUSY;
    }

    memset(&app_h563_ota_master_status, 0,
           sizeof(app_h563_ota_master_status));
    app_h563_ota_master_status.phase = APP_H563_OTA_PHASE_QUEUED;
    app_h563_ota_master_status.unit_id = unit_id;
    app_h563_ota_master_status.requested_baud_rate = preferred_baud_rate;
    app_h563_ota_master_status.image_size = cache_status.expected_size;
    app_h563_ota_master_status.session_id =
        bsp_dwt_get_cycles() ^ tx_time_get() ^ cache_status.image_version;
    if(app_h563_ota_master_status.session_id == 0U)
        app_h563_ota_master_status.session_id = 1U;
    app_h563_ota_master_pending = 1U;
    (void)tx_mutex_put(&app_h563_ota_master_mutex);
    return HAL_OK;
}

uint8_t app_h563_ota_master_has_pending(void)
{
    uint8_t pending = 0U;

    if(app_h563_ota_master_initialized != 0U &&
       tx_mutex_get(&app_h563_ota_master_mutex, TX_NO_WAIT) == TX_SUCCESS)
    {
        pending = app_h563_ota_master_pending;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
    return pending;
}

void app_h563_ota_master_run_pending(void)
{
    uint8_t unit_id;
    uint8_t result;
    uint32_t preferred_baud_rate;
    uint32_t session_id;
    char message[128];

    if(app_h563_ota_master_initialized == 0U ||
       tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return;
    }
    if(app_h563_ota_master_pending == 0U ||
       app_h563_ota_master_running != 0U)
    {
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
        return;
    }
    app_h563_ota_master_pending = 0U;
    app_h563_ota_master_running = 1U;
    app_h563_ota_master_status.started_ms = app_h563_ota_now_ms();
    unit_id = app_h563_ota_master_status.unit_id;
    preferred_baud_rate = app_h563_ota_master_status.requested_baud_rate;
    session_id = app_h563_ota_master_status.session_id;
    (void)tx_mutex_put(&app_h563_ota_master_mutex);

    (void)app_h563_ota_cache_set_transferring(1U);
    result = app_h563_ota_execute(unit_id,
                                  preferred_baud_rate,
                                  session_id);
    if(app_h563_ota_set_baud_rate(APP_H563_OTA_BASE_BAUD_RATE) != HAL_OK)
    {
        result = APP_H563_OTA_RESULT_BAUD;
    }
    app_h563_ota_clear_receive_queue();
    (void)app_h563_ota_cache_set_transferring(0U);

    if(tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        app_h563_ota_master_running = 0U;
        app_h563_ota_master_status.result = result;
        app_h563_ota_master_status.phase =
            (result == APP_H563_OTA_RESULT_OK) ?
            APP_H563_OTA_PHASE_COMPLETE : APP_H563_OTA_PHASE_FAILED;
        app_h563_ota_master_status.finished_ms = app_h563_ota_now_ms();
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }

    (void)snprintf(message,
                   sizeof(message),
                   "H563 OTA %s: unit=%u baud=%lu bytes=%lu result=%s remote=%u\r\n",
                   (result == APP_H563_OTA_RESULT_OK) ? "complete" : "failed",
                   (unsigned int)unit_id,
                   (unsigned long)app_h563_ota_master_status.active_baud_rate,
                   (unsigned long)app_h563_ota_master_status.transferred_size,
                   app_h563_ota_result_name(result),
                   (unsigned int)app_h563_ota_master_status.last_remote_status);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}

void app_h563_ota_master_get_status(app_h563_ota_master_status_t *status)
{
    if(status == NULL)
        return;
    if(app_h563_ota_master_initialized != 0U &&
       tx_mutex_get(&app_h563_ota_master_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        *status = app_h563_ota_master_status;
        (void)tx_mutex_put(&app_h563_ota_master_mutex);
    }
    else
    {
        memset(status, 0, sizeof(*status));
    }
}

const char *app_h563_ota_phase_name(uint8_t phase)
{
    static const char *const names[] =
    {
        "idle", "queued", "hello", "baud_test", "begin", "transfer",
        "verify", "activate", "complete", "failed"
    };

    return (phase < (sizeof(names) / sizeof(names[0]))) ?
           names[phase] : "unknown";
}

const char *app_h563_ota_result_name(uint8_t result)
{
    static const char *const names[] =
    {
        "none", "ok", "cache_not_ready", "transport", "protocol",
        "remote", "baud", "busy"
    };

    return (result < (sizeof(names) / sizeof(names[0]))) ?
           names[result] : "unknown";
}
