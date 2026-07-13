/**
 * @file modbus_app.c
 * @brief Default non-LDC Modbus RTU slave example backed by ld_modbus.
 *
 * The UART BSP records a timestamp for every received byte. This service uses
 * those timestamps to reject T1.5 frame breaks and to commit a frame after
 * T3.5 of bus silence, then hands only complete ADUs to ld_modbus.
 */

#include "modbus_app.h"

#include <stddef.h>
#include <string.h>

#include "bsp_microtime.h"
#include "bsp_uart.h"
#include "ld_modbus_server.h"
#include "modbus_app_config.h"
#include "ld_modbus_rtu_framer.h"

#define MODBUS_APP_FRAME_MAX       LD_MODBUS_RTU_MAX_ADU_LENGTH
#define MODBUS_APP_READ_CHUNK      (16U)
#define MODBUS_APP_HOLDING_COUNT   (64U)
#define MODBUS_APP_INPUT_COUNT     (16U)
#define MODBUS_APP_SIGNATURE       (0x0563U)

static uint8_t g_active_frame[MODBUS_APP_FRAME_MAX];
static uint8_t g_ready_frame[MODBUS_APP_FRAME_MAX];
static uint8_t g_request_frame[MODBUS_APP_FRAME_MAX];
static uint8_t g_response_frame[MODBUS_APP_FRAME_MAX];
static uint8_t g_rx_bytes[MODBUS_APP_READ_CHUNK];
static uint32_t g_rx_time_us[MODBUS_APP_READ_CHUNK];
static uint16_t g_holding_registers[MODBUS_APP_HOLDING_COUNT];
static uint16_t g_input_registers[MODBUS_APP_INPUT_COUNT];
static ld_modbus_server_map_t g_map;
static ld_modbus_rtu_framer_t g_rtu_framer;
static uint32_t g_rx_frames;
static uint32_t g_tx_frames;
static uint32_t g_crc_errors;
static uint32_t g_protocol_errors;
static uint8_t g_initialized;

/** @brief Refresh visible diagnostic registers in the static Modbus map. */
static void modbus_app_update_diagnostics(void)
{
    g_holding_registers[0] = MODBUS_APP_SIGNATURE;
    g_holding_registers[1] = (uint16_t)g_rx_frames;
    g_holding_registers[2] = (uint16_t)g_tx_frames;
    g_holding_registers[3] = (uint16_t)g_crc_errors;
    g_holding_registers[4] = (uint16_t)g_protocol_errors;
    g_holding_registers[5] = (uint16_t)g_rtu_framer.diag.t15_violations;
    g_holding_registers[6] = (uint16_t)g_rtu_framer.t15_us;
    g_holding_registers[7] = (uint16_t)g_rtu_framer.t35_us;
    g_input_registers[0] = MODBUS_APP_SIGNATURE;
    g_input_registers[1] = g_holding_registers[1];
    g_input_registers[2] = g_holding_registers[2];
}

/** @brief Initialize the static server register map. */
static void modbus_app_init_map(void)
{
    memset(g_holding_registers, 0, sizeof(g_holding_registers));
    memset(g_input_registers, 0, sizeof(g_input_registers));
    memset(&g_map, 0, sizeof(g_map));

    g_map.holding_registers = g_holding_registers;
    g_map.holding_registers_count = MODBUS_APP_HOLDING_COUNT;
    g_map.input_registers = g_input_registers;
    g_map.input_registers_count = MODBUS_APP_INPUT_COUNT;
}

/** @brief Process one complete RTU request ADU and send a response when needed. */
static void modbus_app_process_frame(const uint8_t *request, uint16_t request_length)
{
    ld_modbus_server_action_t action = LD_MODBUS_SERVER_ACTION_IGNORED;
    ld_modbus_status_t status;
    size_t response_length = 0U;

    if((request == NULL) || (request_length == 0U))
    {
        return;
    }

    g_rx_frames++;
    modbus_app_update_diagnostics();
    status = ld_modbus_server_process_rtu_adu(&g_map,
                                              MODBUS_APP_UNIT_ID,
                                              request,
                                              request_length,
                                              g_response_frame,
                                              sizeof(g_response_frame),
                                              &response_length,
                                              &action);
    if(status != LD_MODBUS_STATUS_OK)
    {
        if(status == LD_MODBUS_STATUS_BAD_CRC)
        {
            g_crc_errors++;
        }
        else
        {
            g_protocol_errors++;
        }
        modbus_app_update_diagnostics();
        return;
    }

    if((action == LD_MODBUS_SERVER_ACTION_REPLY) &&
       (response_length != 0U) &&
       (response_length <= MODBUS_APP_FRAME_MAX) &&
       (bsp_uart_write(MODBUS_APP_UART_ROLE,
                       g_response_frame,
                       (uint32_t)response_length,
                       MODBUS_APP_TX_TIMEOUT_MS) == BSP_STATUS_OK))
    {
        g_tx_frames++;
        modbus_app_update_diagnostics();
    }
}

/** @brief Consume all completed frames currently owned by the RTU timing layer. */
static void modbus_app_drain_frames(void)
{
    uint16_t request_length = 0U;

    while(ld_modbus_rtu_framer_take(&g_rtu_framer,
                                    g_request_frame,
                                    sizeof(g_request_frame),
                                    &request_length))
    {
        modbus_app_process_frame(g_request_frame, request_length);
    }
}

/** @brief Initialize UART, static register maps, and RTU timing state. */
bsp_status_t modbus_app_init(void)
{
    bsp_uart_config_t uart_config =
    {
        MODBUS_APP_UART_BAUDRATE,
        MODBUS_APP_UART_RX_CHUNK_BYTES
    };
    uint32_t baud_rate = 0U;
    bsp_status_t status;

    modbus_app_init_map();

    status = bsp_uart_init(MODBUS_APP_UART_ROLE, &uart_config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    status = bsp_uart_get_baud_rate(MODBUS_APP_UART_ROLE, &baud_rate);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    if(!ld_modbus_rtu_framer_init(&g_rtu_framer,
                                  g_active_frame,
                                  g_ready_frame,
                                  sizeof(g_active_frame),
                                  baud_rate,
                                  MODBUS_APP_RTU_BITS_PER_CHAR))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    modbus_app_update_diagnostics();
    g_initialized = 1U;
    return BSP_STATUS_OK;
}

/** @brief Run bounded Modbus receive and response work from the superloop. */
void modbus_app_poll(void)
{
    uint32_t length = 0U;

    if(g_initialized == 0U)
    {
        return;
    }

    ld_modbus_rtu_framer_poll(&g_rtu_framer, bsp_microtime_now_us());
    modbus_app_drain_frames();

    if(bsp_uart_try_read_timed(MODBUS_APP_UART_ROLE,
                               g_rx_bytes,
                               g_rx_time_us,
                               sizeof(g_rx_bytes),
                               &length) != BSP_STATUS_OK)
    {
        return;
    }

    for(uint32_t index = 0U; index < length; index++)
    {
        ld_modbus_rtu_framer_on_byte(&g_rtu_framer,
                                     g_rx_bytes[index],
                                     g_rx_time_us[index]);
    }

    ld_modbus_rtu_framer_poll(&g_rtu_framer, bsp_microtime_now_us());
    modbus_app_drain_frames();
    modbus_app_update_diagnostics();
}
