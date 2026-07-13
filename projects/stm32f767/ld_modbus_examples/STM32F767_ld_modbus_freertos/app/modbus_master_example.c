/**
 * @file modbus_master_example.c
 * @brief Periodic static-memory Modbus RTU FC03 master demonstration.
 */

#include "modbus_master_example.h"

#include <string.h>

#include "ld_modbus.h"
#include "ld_modbus_client.h"
#include "ld_modbus_rtu_framer.h"
#include "modbus_app_config.h"
#include "modbus_port.h"

typedef enum
{
    MODBUS_MASTER_STATE_IDLE = 0,
    MODBUS_MASTER_STATE_WAIT_RESPONSE
} modbus_master_state_t;

volatile modbus_master_report_t g_modbus_master_report;

static uint8_t g_active_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_ready_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_request_pdu[LD_MODBUS_MAX_PDU_LENGTH];
static ld_modbus_rtu_framer_t g_framer;
static modbus_master_state_t g_state;
static uint32_t g_deadline_ms;

/** @brief Return true once a wrapping millisecond deadline has expired. */
static bool modbus_master_deadline_reached(uint32_t now_ms,
                                           uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/** @brief Build and send one FC03 request for two holding registers. */
static void modbus_master_send_request(uint32_t now_ms)
{
    ld_modbus_status_t status;
    size_t pdu_length = 0U;
    size_t adu_length = 0U;

    status = ld_modbus_client_build_read_request(
        LD_MODBUS_FC_READ_HOLDING_REGISTERS,
        MODBUS_MASTER_READ_ADDRESS,
        MODBUS_MASTER_READ_QUANTITY,
        g_request_pdu,
        sizeof(g_request_pdu),
        &pdu_length);
    if(status == LD_MODBUS_STATUS_OK)
    {
        status = ld_modbus_rtu_encode(MODBUS_APP_UNIT_ID,
                                      g_request_pdu,
                                      pdu_length,
                                      g_request,
                                      sizeof(g_request),
                                      &adu_length);
    }

    if((status == LD_MODBUS_STATUS_OK) &&
       (modbus_port_write(g_request,
                          (uint16_t)adu_length,
                          MODBUS_APP_TX_TIMEOUT_MS) == HAL_OK))
    {
        g_modbus_master_report.requests_sent++;
        g_state = MODBUS_MASTER_STATE_WAIT_RESPONSE;
        g_deadline_ms = now_ms + MODBUS_MASTER_RESPONSE_MS;
    }
    else
    {
        g_modbus_master_report.parse_errors++;
        g_deadline_ms = now_ms + MODBUS_MASTER_PERIOD_MS;
    }
}

/** @brief Validate and consume one FC03 response for the current request. */
static void modbus_master_process_response(const uint8_t *response,
                                           uint16_t response_length,
                                           uint32_t now_ms)
{
    ld_modbus_adu_view_t view;
    ld_modbus_status_t status;
    uint16_t registers[MODBUS_MASTER_READ_QUANTITY];
    uint8_t exception = 0U;

    status = ld_modbus_rtu_decode(response, response_length, &view);
    if((status == LD_MODBUS_STATUS_OK) &&
       (view.unit_id == MODBUS_APP_UNIT_ID))
    {
        status = ld_modbus_client_parse_read_registers_response(
            LD_MODBUS_FC_READ_HOLDING_REGISTERS,
            MODBUS_MASTER_READ_QUANTITY,
            view.pdu,
            view.pdu_length,
            registers,
            MODBUS_MASTER_READ_QUANTITY,
            &exception);
    }

    if(status == LD_MODBUS_STATUS_OK)
    {
        g_modbus_master_report.last_registers[0] = registers[0];
        g_modbus_master_report.last_registers[1] = registers[1];
        g_modbus_master_report.responses_received++;
    }
    else
    {
        g_modbus_master_report.last_exception = exception;
        g_modbus_master_report.parse_errors++;
    }

    g_state = MODBUS_MASTER_STATE_IDLE;
    g_deadline_ms = now_ms + MODBUS_MASTER_PERIOD_MS;
}

/** @brief Initialize strict framing, USART3, and the periodic request state. */
HAL_StatusTypeDef modbus_master_example_init(void)
{
    memset((void *)&g_modbus_master_report, 0,
           sizeof(g_modbus_master_report));

    if(!ld_modbus_rtu_framer_init(&g_framer,
                                  g_active_frame,
                                  g_ready_frame,
                                  sizeof(g_active_frame),
                                  MODBUS_APP_BAUD_RATE,
                                  MODBUS_APP_BITS_PER_CHAR))
    {
        return HAL_ERROR;
    }

    g_state = MODBUS_MASTER_STATE_IDLE;
    g_deadline_ms = HAL_GetTick() + MODBUS_MASTER_PERIOD_MS;
    return modbus_port_init(MODBUS_APP_BAUD_RATE);
}

/** @brief Drain received bytes and advance the periodic FC03 transaction. */
void modbus_master_example_poll(void)
{
    uint8_t byte;
    uint32_t timestamp_us;
    uint16_t response_length;
    uint32_t now_ms;

    while(modbus_port_try_read(&byte, &timestamp_us))
    {
        ld_modbus_rtu_framer_on_byte(&g_framer, byte, timestamp_us);
    }

    ld_modbus_rtu_framer_poll(&g_framer, modbus_port_time_us());
    now_ms = HAL_GetTick();

    while(ld_modbus_rtu_framer_take(&g_framer,
                                    g_response,
                                    sizeof(g_response),
                                    &response_length))
    {
        if(g_state == MODBUS_MASTER_STATE_WAIT_RESPONSE)
        {
            modbus_master_process_response(g_response,
                                           response_length,
                                           now_ms);
        }
    }

    if((g_state == MODBUS_MASTER_STATE_WAIT_RESPONSE) &&
       modbus_master_deadline_reached(now_ms, g_deadline_ms))
    {
        g_modbus_master_report.response_timeouts++;
        g_state = MODBUS_MASTER_STATE_IDLE;
        g_deadline_ms = now_ms + MODBUS_MASTER_PERIOD_MS;
    }
    else if((g_state == MODBUS_MASTER_STATE_IDLE) &&
            modbus_master_deadline_reached(now_ms, g_deadline_ms))
    {
        modbus_master_send_request(now_ms);
    }

    g_modbus_master_report.t15_violations = g_framer.diag.t15_violations;
}
