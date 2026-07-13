/**
 * @file modbus_loopback.c
 * @brief Bare-metal Modbus RTU master/slave loopback and timing fault tests.
 *
 * USART2 is the test master and UART4 is the server. Both ports use one-byte
 * interrupt reception so each byte receives a TIM2 microsecond timestamp.
 * ISRs only append bytes to static rings; framing and protocol work run from
 * modbus_loopback_poll(). The board's MAX13487 transceivers own direction.
 */

#include "modbus_loopback.h"
#include "modbus_example_config.h"

#if MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_LOOPBACK

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ld_modbus.h"
#include "ld_modbus_client.h"
#include "ld_modbus_server.h"
#include "modbus_loopback_config.h"
#include "ld_modbus_rtu_framer.h"
#include "tim.h"
#include "usart.h"

#define MODBUS_LOOPBACK_REPORT_MAGIC       (0x4C444D42UL)
#define MODBUS_LOOPBACK_FRAME_MAX          LD_MODBUS_RTU_MAX_ADU_LENGTH
#define MODBUS_LOOPBACK_BAUD_COUNT         (3U)
#define MODBUS_LOOPBACK_EXPECT_NONE        (0U)
#define MODBUS_LOOPBACK_EXPECT_READ        (1U)
#define MODBUS_LOOPBACK_EXPECT_WRITE       (2U)

typedef struct
{
    UART_HandleTypeDef *uart;
    uint8_t rx_byte;
    uint8_t ring_data[MODBUS_LOOPBACK_RX_RING_BYTES];
    uint32_t ring_timestamp_us[MODBUS_LOOPBACK_RX_RING_BYTES];
    volatile uint16_t write_index;
    uint16_t read_index;
    uint32_t uart_errors;
    uint32_t ring_overflows;
    uint8_t active_frame[MODBUS_LOOPBACK_FRAME_MAX];
    uint8_t ready_frame[MODBUS_LOOPBACK_FRAME_MAX];
    ld_modbus_rtu_framer_t timing;
} modbus_loopback_port_t;

typedef struct
{
    uint8_t kind;
    uint8_t is_ready;
    uint16_t value;
} modbus_loopback_response_t;

static const uint32_t g_test_baud_rates[MODBUS_LOOPBACK_BAUD_COUNT] =
{
    9600U,
    19200U,
    115200U
};

volatile modbus_loopback_report_t g_modbus_loopback_report;

static modbus_loopback_port_t g_master_port;
static modbus_loopback_port_t g_slave_port;
static modbus_loopback_response_t g_master_response;
static ld_modbus_server_map_t g_server_map;
static uint16_t g_holding_registers[MODBUS_LOOPBACK_HOLDING_COUNT];
static uint8_t g_master_request[MODBUS_LOOPBACK_FRAME_MAX];
static uint8_t g_slave_request[MODBUS_LOOPBACK_FRAME_MAX];
static uint8_t g_slave_response[MODBUS_LOOPBACK_FRAME_MAX];
static uint8_t g_baud_index;
static uint16_t g_master_request_length;
static uint16_t g_expected_write_value;
static uint32_t g_slave_t15_total;
static uint32_t g_state_deadline_us;
static uint32_t g_led_deadline_ms;

/** @brief Return the wrapping 1 MHz TIM2 count. */
static uint32_t modbus_loopback_now_us(void)
{
    return __HAL_TIM_GET_COUNTER(&htim2);
}

/** @brief Return true when a wrapping microsecond deadline has expired. */
static bool modbus_loopback_deadline_expired(uint32_t now_us, uint32_t deadline_us)
{
    return (int32_t)(now_us - deadline_us) >= 0;
}

/** @brief Enter a test state and expose it to the debugger. */
static void modbus_loopback_set_state(modbus_loopback_state_t state)
{
    g_modbus_loopback_report.state = state;
}

/** @brief Stop the automatic test at the first deterministic failure. */
static void modbus_loopback_fail(modbus_loopback_error_t error)
{
    g_modbus_loopback_report.last_error = error;
    g_modbus_loopback_report.failed_checks++;
    modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_FAIL);
}

/** @brief Record one completed positive or negative test check. */
static void modbus_loopback_pass_check(void)
{
    g_modbus_loopback_report.passed_checks++;
}

/** @brief Reset one port's software receive state while UART reception is stopped. */
static bool modbus_loopback_port_reset(modbus_loopback_port_t *port,
                                       uint32_t baud_rate)
{
    if((port == NULL) ||
       !ld_modbus_rtu_framer_init(&port->timing,
                                  port->active_frame,
                                  port->ready_frame,
                                  sizeof(port->active_frame),
                                  baud_rate,
                                  MODBUS_LOOPBACK_BITS_PER_CHAR))
    {
        return false;
    }

    port->write_index = 0U;
    port->read_index = 0U;
    port->uart_errors = 0U;
    port->ring_overflows = 0U;
    return true;
}

/** @brief Arm one-byte interrupt reception for a port. */
static HAL_StatusTypeDef modbus_loopback_port_start_rx(modbus_loopback_port_t *port)
{
    if((port == NULL) || (port->uart == NULL))
    {
        return HAL_ERROR;
    }
    return HAL_UART_Receive_IT(port->uart, &port->rx_byte, 1U);
}

/** @brief Stop local reception, transmit one chunk, and restore reception. */
static HAL_StatusTypeDef modbus_loopback_port_transmit(modbus_loopback_port_t *port,
                                                       const uint8_t *data,
                                                       uint16_t length)
{
    HAL_StatusTypeDef status;

    if((port == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    (void)HAL_UART_AbortReceive(port->uart);
    status = HAL_UART_Transmit(port->uart,
                               (uint8_t *)data,
                               length,
                               MODBUS_LOOPBACK_UART_TIMEOUT_MS);
    if(modbus_loopback_port_start_rx(port) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return status;
}

/** @brief Push one ISR-owned byte into the selected static ring. */
static void modbus_loopback_port_on_rx_isr(modbus_loopback_port_t *port)
{
    uint16_t next;

    if(port == NULL)
    {
        return;
    }

    next = (uint16_t)((port->write_index + 1U) % MODBUS_LOOPBACK_RX_RING_BYTES);
    if(next == port->read_index)
    {
        port->ring_overflows++;
    }
    else
    {
        port->ring_data[port->write_index] = port->rx_byte;
        port->ring_timestamp_us[port->write_index] = modbus_loopback_now_us();
        port->write_index = next;
    }

    if(modbus_loopback_port_start_rx(port) != HAL_OK)
    {
        port->uart_errors++;
    }
}

/** @brief Drain timestamped ISR bytes into the RTU timing state machine. */
static void modbus_loopback_port_poll(modbus_loopback_port_t *port,
                                      uint32_t now_us)
{
    if(port == NULL)
    {
        return;
    }

    while(port->read_index != port->write_index)
    {
        const uint16_t index = port->read_index;
        const uint8_t byte = port->ring_data[index];
        const uint32_t timestamp_us = port->ring_timestamp_us[index];

        port->read_index =
            (uint16_t)((port->read_index + 1U) % MODBUS_LOOPBACK_RX_RING_BYTES);
        ld_modbus_rtu_framer_on_byte(&port->timing, byte, timestamp_us);
    }
    ld_modbus_rtu_framer_poll(&port->timing, now_us);
}

/** @brief Reconfigure both generated UART handles to one common baud rate. */
static HAL_StatusTypeDef modbus_loopback_set_baud_rate(uint32_t baud_rate)
{
    (void)HAL_UART_AbortReceive(&huart2);
    (void)HAL_UART_AbortReceive(&huart4);

    if(g_modbus_loopback_report.active_baud_rate != 0U)
    {
        g_slave_t15_total += g_slave_port.timing.diag.t15_violations;
    }

    huart2.Init.BaudRate = baud_rate;
    huart4.Init.BaudRate = baud_rate;
    if((HAL_UART_Init(&huart2) != HAL_OK) ||
       (HAL_UART_Init(&huart4) != HAL_OK) ||
       !modbus_loopback_port_reset(&g_master_port, baud_rate) ||
       !modbus_loopback_port_reset(&g_slave_port, baud_rate) ||
       (modbus_loopback_port_start_rx(&g_master_port) != HAL_OK) ||
       (modbus_loopback_port_start_rx(&g_slave_port) != HAL_OK))
    {
        return HAL_ERROR;
    }

    g_modbus_loopback_report.active_baud_rate = baud_rate;
    g_modbus_loopback_report.t15_us = g_slave_port.timing.t15_us;
    g_modbus_loopback_report.t35_us = g_slave_port.timing.t35_us;
    return HAL_OK;
}

/** @brief Build one complete RTU request with a selected unit identifier. */
static bool modbus_loopback_build_read_request(uint8_t unit_id,
                                               uint16_t address,
                                               uint8_t *adu,
                                               uint16_t *adu_length)
{
    uint8_t pdu[5];
    size_t pdu_length = 0U;
    size_t frame_length = 0U;

    if((adu == NULL) || (adu_length == NULL) ||
       (ld_modbus_client_build_read_request(
            LD_MODBUS_FC_READ_HOLDING_REGISTERS,
            address,
            1U,
            pdu,
            sizeof(pdu),
            &pdu_length) != LD_MODBUS_STATUS_OK) ||
       (ld_modbus_rtu_encode(unit_id,
                             pdu,
                             pdu_length,
                             adu,
                             MODBUS_LOOPBACK_FRAME_MAX,
                             &frame_length) != LD_MODBUS_STATUS_OK) ||
       (frame_length > UINT16_MAX))
    {
        return false;
    }

    *adu_length = (uint16_t)frame_length;
    return true;
}

/** @brief Build one function-06 request for the loopback holding map. */
static bool modbus_loopback_build_write_request(uint16_t value,
                                                uint8_t *adu,
                                                uint16_t *adu_length)
{
    uint8_t pdu[5];
    size_t pdu_length = 0U;
    size_t frame_length = 0U;

    if((adu == NULL) || (adu_length == NULL) ||
       (ld_modbus_client_build_write_single_register(1U,
                                                      value,
                                                      pdu,
                                                      sizeof(pdu),
                                                      &pdu_length) !=
        LD_MODBUS_STATUS_OK) ||
       (ld_modbus_rtu_encode(MODBUS_LOOPBACK_UNIT_ID,
                             pdu,
                             pdu_length,
                             adu,
                             MODBUS_LOOPBACK_FRAME_MAX,
                             &frame_length) != LD_MODBUS_STATUS_OK) ||
       (frame_length > UINT16_MAX))
    {
        return false;
    }

    *adu_length = (uint16_t)frame_length;
    return true;
}

/** @brief Clear the expected master response before starting a new request. */
static void modbus_loopback_expect_response(uint8_t kind)
{
    g_master_response.kind = kind;
    g_master_response.is_ready = 0U;
    g_master_response.value = 0U;
}

/** @brief Parse one master-side frame only when it matches the active test. */
static void modbus_loopback_process_master_frame(const uint8_t *frame,
                                                 uint16_t length)
{
    ld_modbus_adu_view_t view;
    ld_modbus_status_t status;
    uint8_t exception = 0U;
    uint16_t value = 0U;

    g_modbus_loopback_report.master_rx_frames++;
    status = ld_modbus_rtu_decode(frame, length, &view);
    if((status != LD_MODBUS_STATUS_OK) ||
       (view.unit_id != MODBUS_LOOPBACK_UNIT_ID) ||
       (view.pdu_length == 0U) ||
       (g_master_response.kind == MODBUS_LOOPBACK_EXPECT_NONE))
    {
        return;
    }

    if((g_master_response.kind == MODBUS_LOOPBACK_EXPECT_READ) &&
       (ld_modbus_client_parse_read_registers_response(
            LD_MODBUS_FC_READ_HOLDING_REGISTERS,
            1U,
            view.pdu,
            view.pdu_length,
            &value,
            1U,
            &exception) == LD_MODBUS_STATUS_OK))
    {
        g_master_response.value = value;
        g_master_response.is_ready = 1U;
    }
    else if((g_master_response.kind == MODBUS_LOOPBACK_EXPECT_WRITE) &&
            (ld_modbus_client_parse_write_response(
                 LD_MODBUS_FC_WRITE_SINGLE_REGISTER,
                 1U,
                 g_expected_write_value,
                 view.pdu,
                 view.pdu_length,
                 &exception) == LD_MODBUS_STATUS_OK))
    {
        g_master_response.value = g_expected_write_value;
        g_master_response.is_ready = 1U;
    }
}

/** @brief Process one UART4 request and transmit a reply when required. */
static void modbus_loopback_process_slave_frame(const uint8_t *frame,
                                                uint16_t length)
{
    ld_modbus_server_action_t action = LD_MODBUS_SERVER_ACTION_IGNORED;
    ld_modbus_status_t status;
    size_t response_length = 0U;

    g_modbus_loopback_report.slave_rx_frames++;
    status = ld_modbus_server_process_rtu_adu(&g_server_map,
                                              MODBUS_LOOPBACK_UNIT_ID,
                                              frame,
                                              length,
                                              g_slave_response,
                                              sizeof(g_slave_response),
                                              &response_length,
                                              &action);
    if(status == LD_MODBUS_STATUS_BAD_CRC)
    {
        g_modbus_loopback_report.slave_bad_crc_frames++;
        return;
    }
    if((status != LD_MODBUS_STATUS_OK) ||
       (action != LD_MODBUS_SERVER_ACTION_REPLY) ||
       (response_length == 0U) ||
       (response_length > UINT16_MAX))
    {
        return;
    }

    if(modbus_loopback_port_transmit(&g_slave_port,
                                     g_slave_response,
                                     (uint16_t)response_length) != HAL_OK)
    {
        modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
        return;
    }
    g_modbus_loopback_report.slave_tx_frames++;
}

/** @brief Drain every complete frame from one RTU timing context. */
static void modbus_loopback_drain_port(modbus_loopback_port_t *port,
                                       bool is_slave)
{
    uint16_t length;
    uint8_t *buffer = is_slave ? g_slave_request : g_master_request;

    while(ld_modbus_rtu_framer_take(&port->timing,
                                    buffer,
                                    MODBUS_LOOPBACK_FRAME_MAX,
                                    &length))
    {
        if(is_slave)
        {
            modbus_loopback_process_slave_frame(buffer, length);
        }
        else
        {
            modbus_loopback_process_master_frame(buffer, length);
        }
    }
}

/** @brief Send the currently built request through the USART2 master. */
static bool modbus_loopback_send_master_request(void)
{
    return modbus_loopback_port_transmit(&g_master_port,
                                         g_master_request,
                                         g_master_request_length) == HAL_OK;
}

/** @brief Update the active-low status LED without blocking the test. */
static void modbus_loopback_update_led(void)
{
    const uint32_t now_ms = HAL_GetTick();

    if(g_modbus_loopback_report.state == MODBUS_LOOPBACK_STATE_PASS)
    {
        HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, GPIO_PIN_RESET);
    }
    else if((g_modbus_loopback_report.state == MODBUS_LOOPBACK_STATE_FAIL) &&
            ((int32_t)(now_ms - g_led_deadline_ms) >= 0))
    {
        HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
        g_led_deadline_ms = now_ms + 100U;
    }
    else if((g_modbus_loopback_report.state != MODBUS_LOOPBACK_STATE_FAIL) &&
            ((int32_t)(now_ms - g_led_deadline_ms) >= 0))
    {
        HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
        g_led_deadline_ms = now_ms + 500U;
    }
}

/** @brief Initialize static register storage and public diagnostics. */
static void modbus_loopback_initialize_data(void)
{
    memset((void *)&g_modbus_loopback_report, 0, sizeof(g_modbus_loopback_report));
    memset(g_holding_registers, 0, sizeof(g_holding_registers));
    memset(&g_server_map, 0, sizeof(g_server_map));
    g_modbus_loopback_report.magic = MODBUS_LOOPBACK_REPORT_MAGIC;
    g_slave_t15_total = 0U;
    g_holding_registers[0] = 0x0563U;
    g_holding_registers[1] = 0U;
    g_server_map.holding_registers = g_holding_registers;
    g_server_map.holding_registers_count = MODBUS_LOOPBACK_HOLDING_COUNT;
}

/**
 * @brief Start TIM2, initialize both RTU receive paths, and arm UART interrupts.
 * @return HAL_OK on success; otherwise the report contains the failed stage.
 */
HAL_StatusTypeDef modbus_loopback_init(void)
{
    modbus_loopback_initialize_data();
    g_master_port.uart = &huart2;
    g_slave_port.uart = &huart4;

    if(HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_TIMER);
        return HAL_ERROR;
    }

    if(modbus_loopback_set_baud_rate(g_test_baud_rates[0]) != HAL_OK)
    {
        modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_RX_START);
        return HAL_ERROR;
    }

    g_baud_index = 0U;
    g_led_deadline_ms = HAL_GetTick();
    g_state_deadline_us =
        modbus_loopback_now_us() + (MODBUS_LOOPBACK_STARTUP_WAIT_MS * 1000U);
    modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_STARTUP);
    return HAL_OK;
}

/** @brief Run the sequential positive, negative, T1.5, and T3.5 tests. */
static void modbus_loopback_run_test(uint32_t now_us)
{
    switch(g_modbus_loopback_report.state)
    {
        case MODBUS_LOOPBACK_STATE_STARTUP:
            if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_READ_REQUEST);
            }
            break;

        case MODBUS_LOOPBACK_STATE_CONFIGURE_BAUD:
            if(modbus_loopback_set_baud_rate(g_test_baud_rates[g_baud_index]) != HAL_OK)
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_RECONFIGURE);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_STARTUP_WAIT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_STARTUP);
            break;

        case MODBUS_LOOPBACK_STATE_READ_REQUEST:
            if(!modbus_loopback_build_read_request(MODBUS_LOOPBACK_UNIT_ID,
                                                    0U,
                                                    g_master_request,
                                                    &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_READ);
            if(!modbus_loopback_send_master_request())
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_RESPONSE_TIMEOUT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_READ_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_READ_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                if(g_master_response.value != 0x0563U)
                {
                    modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_READ_VALUE);
                    break;
                }
                g_modbus_loopback_report.last_read_value = g_master_response.value;
                modbus_loopback_pass_check();
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_WRITE_REQUEST);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_READ_TIMEOUT);
            }
            break;

        case MODBUS_LOOPBACK_STATE_WRITE_REQUEST:
            g_expected_write_value = (uint16_t)(0x1200U + g_baud_index);
            if(!modbus_loopback_build_write_request(g_expected_write_value,
                                                     g_master_request,
                                                     &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_WRITE);
            if(!modbus_loopback_send_master_request())
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_RESPONSE_TIMEOUT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_WRITE_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_WRITE_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                if((g_master_response.value != g_expected_write_value) ||
                   (g_holding_registers[1] != g_expected_write_value))
                {
                    modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_WRITE_ECHO);
                    break;
                }
                g_modbus_loopback_report.last_written_value = g_expected_write_value;
                modbus_loopback_pass_check();
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_BAD_CRC_REQUEST);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_WRITE_TIMEOUT);
            }
            break;

        case MODBUS_LOOPBACK_STATE_BAD_CRC_REQUEST:
            if(!modbus_loopback_build_read_request(MODBUS_LOOPBACK_UNIT_ID,
                                                    0U,
                                                    g_master_request,
                                                    &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            g_master_request[g_master_request_length - 1U] ^= 0x5AU;
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_READ);
            if(!modbus_loopback_send_master_request())
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_NEGATIVE_WAIT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_BAD_CRC_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_BAD_CRC_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                modbus_loopback_fail(
                    MODBUS_LOOPBACK_ERROR_UNEXPECTED_BAD_CRC_RESPONSE);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_pass_check();
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_WRONG_UNIT_REQUEST);
            }
            break;

        case MODBUS_LOOPBACK_STATE_WRONG_UNIT_REQUEST:
            if(!modbus_loopback_build_read_request(2U,
                                                    0U,
                                                    g_master_request,
                                                    &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_READ);
            if(!modbus_loopback_send_master_request())
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_NEGATIVE_WAIT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_WRONG_UNIT_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_WRONG_UNIT_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                modbus_loopback_fail(
                    MODBUS_LOOPBACK_ERROR_UNEXPECTED_WRONG_UNIT_RESPONSE);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_pass_check();
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_T15_FIRST_PART);
            }
            break;

        case MODBUS_LOOPBACK_STATE_T15_FIRST_PART:
            if(!modbus_loopback_build_read_request(MODBUS_LOOPBACK_UNIT_ID,
                                                    0U,
                                                    g_master_request,
                                                    &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_READ);
            if(modbus_loopback_port_transmit(&g_master_port,
                                             g_master_request,
                                             3U) != HAL_OK)
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us = modbus_loopback_now_us() +
                ((g_slave_port.timing.t15_us + g_slave_port.timing.t35_us) / 2U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_T15_GAP_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_T15_GAP_WAIT:
            if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_T15_SECOND_PART);
            }
            break;

        case MODBUS_LOOPBACK_STATE_T15_SECOND_PART:
            if(modbus_loopback_port_transmit(&g_master_port,
                                             &g_master_request[3],
                                             (uint16_t)(g_master_request_length - 3U)) != HAL_OK)
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_NEGATIVE_WAIT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_T15_RESPONSE_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_T15_RESPONSE_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                modbus_loopback_fail(
                    MODBUS_LOOPBACK_ERROR_UNEXPECTED_T15_RESPONSE);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_pass_check();
                modbus_loopback_set_state(
                    MODBUS_LOOPBACK_STATE_T35_RECOVERY_REQUEST);
            }
            break;

        case MODBUS_LOOPBACK_STATE_T35_RECOVERY_REQUEST:
            if(!modbus_loopback_build_read_request(MODBUS_LOOPBACK_UNIT_ID,
                                                    1U,
                                                    g_master_request,
                                                    &g_master_request_length))
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_BUILD_REQUEST);
                break;
            }
            modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_READ);
            if(!modbus_loopback_send_master_request())
            {
                modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_TRANSMIT);
                break;
            }
            g_state_deadline_us =
                now_us + (MODBUS_LOOPBACK_RESPONSE_TIMEOUT_MS * 1000U);
            modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_T35_RECOVERY_WAIT);
            break;

        case MODBUS_LOOPBACK_STATE_T35_RECOVERY_WAIT:
            if(g_master_response.is_ready != 0U)
            {
                if(g_master_response.value != g_expected_write_value)
                {
                    modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_READ_VALUE);
                    break;
                }
                modbus_loopback_pass_check();
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_NEXT_BAUD);
            }
            else if(modbus_loopback_deadline_expired(now_us, g_state_deadline_us))
            {
                modbus_loopback_fail(
                    MODBUS_LOOPBACK_ERROR_T35_RECOVERY_TIMEOUT);
            }
            break;

        case MODBUS_LOOPBACK_STATE_NEXT_BAUD:
            g_modbus_loopback_report.completed_baud_rates++;
            g_baud_index++;
            if(g_baud_index >= MODBUS_LOOPBACK_BAUD_COUNT)
            {
                modbus_loopback_expect_response(MODBUS_LOOPBACK_EXPECT_NONE);
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_PASS);
            }
            else
            {
                modbus_loopback_set_state(MODBUS_LOOPBACK_STATE_CONFIGURE_BAUD);
            }
            break;

        case MODBUS_LOOPBACK_STATE_PASS:
        case MODBUS_LOOPBACK_STATE_FAIL:
        default:
            break;
    }
}

/** @brief Run bounded slave processing and advance the automatic master test. */
void modbus_loopback_poll(void)
{
    const uint32_t now_us = modbus_loopback_now_us();

    modbus_loopback_port_poll(&g_master_port, now_us);
    modbus_loopback_port_poll(&g_slave_port, now_us);
    modbus_loopback_drain_port(&g_slave_port, true);
    modbus_loopback_drain_port(&g_master_port, false);

    g_modbus_loopback_report.slave_t15_violations =
        g_slave_t15_total + g_slave_port.timing.diag.t15_violations;
    g_modbus_loopback_report.uart_errors =
        g_master_port.uart_errors + g_slave_port.uart_errors;
    g_modbus_loopback_report.rx_overflows =
        g_master_port.ring_overflows + g_slave_port.ring_overflows;

    if((g_modbus_loopback_report.state != MODBUS_LOOPBACK_STATE_FAIL) &&
       (g_modbus_loopback_report.rx_overflows != 0U))
    {
        modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_RX_OVERFLOW);
    }
    else if((g_modbus_loopback_report.state != MODBUS_LOOPBACK_STATE_FAIL) &&
            (g_modbus_loopback_report.uart_errors != 0U))
    {
        modbus_loopback_fail(MODBUS_LOOPBACK_ERROR_UART_RUNTIME);
    }
    else
    {
        modbus_loopback_run_test(now_us);
    }

    modbus_loopback_update_led();
}

/** @brief Route the Cube HAL one-byte receive callback to the owned port ring. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart == &huart2)
    {
        modbus_loopback_port_on_rx_isr(&g_master_port);
    }
    else if(huart == &huart4)
    {
        modbus_loopback_port_on_rx_isr(&g_slave_port);
    }
}

/** @brief Record a UART error and re-arm one-byte reception. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    modbus_loopback_port_t *port = NULL;

    if(huart == &huart2)
    {
        port = &g_master_port;
    }
    else if(huart == &huart4)
    {
        port = &g_slave_port;
    }

    if(port != NULL)
    {
        port->uart_errors++;
        (void)HAL_UART_AbortReceive(huart);
        if(modbus_loopback_port_start_rx(port) != HAL_OK)
        {
            port->uart_errors++;
        }
    }
}

#endif /* MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_LOOPBACK */
