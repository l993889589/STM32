/**
 * @file modbus_slave_example.c
 * @brief Minimal static-memory CubeMX USART2 Modbus RTU slave example.
 *
 * CubeMX owns clock, GPIO, USART2, IRQ, and TIM2 initialization. The ISR only
 * stores timestamped bytes in a static ring. Poll context performs T1.5/T3.5
 * framing and calls the platform-independent ld_modbus server.
 */

#include "modbus_slave_example.h"

#include <stddef.h>
#include <string.h>

#include "ld_modbus_rtu_framer.h"
#include "ld_modbus_server.h"
#include "modbus_example_config.h"
#include "tim.h"
#include "usart.h"

#if MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_SLAVE

#define MODBUS_SLAVE_FRAME_MAX LD_MODBUS_RTU_MAX_ADU_LENGTH

volatile modbus_slave_example_report_t g_modbus_slave_report;

static uint8_t g_rx_byte;
static uint8_t g_rx_ring[MODBUS_SLAVE_RX_RING_BYTES];
static uint32_t g_rx_timestamp_us[MODBUS_SLAVE_RX_RING_BYTES];
static volatile uint16_t g_rx_write_index;
static uint16_t g_rx_read_index;
static uint8_t g_active_frame[MODBUS_SLAVE_FRAME_MAX];
static uint8_t g_ready_frame[MODBUS_SLAVE_FRAME_MAX];
static uint8_t g_request[MODBUS_SLAVE_FRAME_MAX];
static uint8_t g_response[MODBUS_SLAVE_FRAME_MAX];
static uint16_t g_holding_registers[MODBUS_SLAVE_HOLDING_COUNT];
static ld_modbus_rtu_framer_t g_framer;
static ld_modbus_server_map_t g_map;
static uint8_t g_initialized;

/** @brief Return the wrapping TIM2 microsecond counter. */
static uint32_t modbus_slave_now_us(void)
{
    return __HAL_TIM_GET_COUNTER(&htim2);
}

/** @brief Arm the next one-byte interrupt receive operation. */
static HAL_StatusTypeDef modbus_slave_start_rx(void)
{
    return HAL_UART_Receive_IT(&huart2, &g_rx_byte, 1U);
}

/** @brief Refresh diagnostic holding registers without dynamic storage. */
static void modbus_slave_update_registers(void)
{
    g_holding_registers[0] = 0x0563U;
    g_holding_registers[1] = (uint16_t)g_modbus_slave_report.rx_frames;
    g_holding_registers[2] = (uint16_t)g_modbus_slave_report.tx_frames;
    g_holding_registers[3] = (uint16_t)g_modbus_slave_report.crc_errors;
    g_holding_registers[4] = (uint16_t)g_modbus_slave_report.protocol_errors;
    g_holding_registers[5] = (uint16_t)g_modbus_slave_report.t15_violations;
    g_holding_registers[6] = (uint16_t)g_modbus_slave_report.t15_us;
    g_holding_registers[7] = (uint16_t)g_modbus_slave_report.t35_us;
}

/** @brief Process one complete request and transmit its optional response. */
static void modbus_slave_process_frame(const uint8_t *request,
                                       uint16_t request_length)
{
    ld_modbus_server_action_t action = LD_MODBUS_SERVER_ACTION_IGNORED;
    ld_modbus_status_t status;
    size_t response_length = 0U;

    g_modbus_slave_report.rx_frames++;
    status = ld_modbus_server_process_rtu_adu(&g_map,
                                              MODBUS_SLAVE_UNIT_ID,
                                              request,
                                              request_length,
                                              g_response,
                                              sizeof(g_response),
                                              &response_length,
                                              &action);
    if(status != LD_MODBUS_STATUS_OK)
    {
        if(status == LD_MODBUS_STATUS_BAD_CRC)
        {
            g_modbus_slave_report.crc_errors++;
        }
        else
        {
            g_modbus_slave_report.protocol_errors++;
        }
        modbus_slave_update_registers();
        return;
    }

    if((action == LD_MODBUS_SERVER_ACTION_REPLY) &&
       (response_length != 0U) &&
       (response_length <= UINT16_MAX))
    {
        (void)HAL_UART_AbortReceive(&huart2);
        if(HAL_UART_Transmit(&huart2,
                             g_response,
                             (uint16_t)response_length,
                             MODBUS_SLAVE_UART_TIMEOUT_MS) == HAL_OK)
        {
            g_modbus_slave_report.tx_frames++;
        }
        else
        {
            g_modbus_slave_report.uart_errors++;
        }
        if(modbus_slave_start_rx() != HAL_OK)
        {
            g_modbus_slave_report.uart_errors++;
        }
    }
    modbus_slave_update_registers();
}

/** @brief Initialize TIM2 framing time and arm USART2 one-byte reception. */
HAL_StatusTypeDef modbus_slave_example_init(void)
{
    memset((void *)&g_modbus_slave_report, 0, sizeof(g_modbus_slave_report));
    memset(g_holding_registers, 0, sizeof(g_holding_registers));
    memset(&g_map, 0, sizeof(g_map));

    huart2.Init.BaudRate = MODBUS_SLAVE_BAUD_RATE;
    if((HAL_UART_Init(&huart2) != HAL_OK) ||
       (HAL_TIM_Base_Start(&htim2) != HAL_OK) ||
       !ld_modbus_rtu_framer_init(&g_framer,
                                  g_active_frame,
                                  g_ready_frame,
                                  sizeof(g_active_frame),
                                  huart2.Init.BaudRate,
                                  MODBUS_SLAVE_BITS_PER_CHAR))
    {
        return HAL_ERROR;
    }

    g_map.holding_registers = g_holding_registers;
    g_map.holding_registers_count = MODBUS_SLAVE_HOLDING_COUNT;
    g_modbus_slave_report.t15_us = g_framer.t15_us;
    g_modbus_slave_report.t35_us = g_framer.t35_us;
    modbus_slave_update_registers();

    if(modbus_slave_start_rx() != HAL_OK)
    {
        return HAL_ERROR;
    }

    g_initialized = 1U;
    return HAL_OK;
}

/** @brief Drain received bytes, complete RTU frames, and send slave replies. */
void modbus_slave_example_poll(void)
{
    uint16_t request_length = 0U;

    if(g_initialized == 0U)
    {
        return;
    }

    while(g_rx_read_index != g_rx_write_index)
    {
        const uint16_t index = g_rx_read_index;

        g_rx_read_index =
            (uint16_t)((g_rx_read_index + 1U) % MODBUS_SLAVE_RX_RING_BYTES);
        ld_modbus_rtu_framer_on_byte(&g_framer,
                                     g_rx_ring[index],
                                     g_rx_timestamp_us[index]);
    }

    ld_modbus_rtu_framer_poll(&g_framer, modbus_slave_now_us());
    while(ld_modbus_rtu_framer_take(&g_framer,
                                    g_request,
                                    sizeof(g_request),
                                    &request_length))
    {
        modbus_slave_process_frame(g_request, request_length);
    }

    g_modbus_slave_report.t15_violations = g_framer.diag.t15_violations;
    modbus_slave_update_registers();
}

/** @brief Store one USART2 byte and its end-of-character TIM2 timestamp. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart == &huart2)
    {
        const uint16_t next =
            (uint16_t)((g_rx_write_index + 1U) % MODBUS_SLAVE_RX_RING_BYTES);

        if(next == g_rx_read_index)
        {
            g_modbus_slave_report.rx_overflows++;
        }
        else
        {
            g_rx_ring[g_rx_write_index] = g_rx_byte;
            g_rx_timestamp_us[g_rx_write_index] = modbus_slave_now_us();
            g_rx_write_index = next;
        }

        if(modbus_slave_start_rx() != HAL_OK)
        {
            g_modbus_slave_report.uart_errors++;
        }
    }
}

/** @brief Record a USART2 error and deterministically restart reception. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart == &huart2)
    {
        g_modbus_slave_report.uart_errors++;
        (void)HAL_UART_AbortReceive(huart);
        if(modbus_slave_start_rx() != HAL_OK)
        {
            g_modbus_slave_report.uart_errors++;
        }
    }
}

#endif /* MODBUS_EXAMPLE_MODE == MODBUS_EXAMPLE_MODE_SLAVE */
