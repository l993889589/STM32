/**
 * @file transport_w800_tcp.c
 * @brief W800 UART/AT binding and bounded Modbus TCP stream framing.
 */

#include "transport_w800_tcp.h"

#include <string.h>

#include "at_module_w800.h"
#include "bsp_uart.h"
#include "w800.h"
#include "osal.h"

#define TRANSPORT_W800_RX_CHUNK_BYTES (64U)
#define TRANSPORT_W800_TX_TIMEOUT_MS  (1000U)

/** @brief Send AT command bytes through the board Wi-Fi UART. */
static int transport_w800_at_write(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    return bsp_uart_write(BOARD_UART_WIFI, data, length,
                          TRANSPORT_W800_TX_TIMEOUT_MS) == BSP_STATUS_OK ?
           (int)length : -1;
}

/** @brief Return OS-neutral monotonic milliseconds to the AT session. */
static uint32_t transport_w800_now_ms(void *arg)
{
    (void)arg;
    return osal_time_get_ms();
}

/** @brief Sleep through the selected bare-metal or ThreadX OS abstraction. */
static void transport_w800_sleep_ms(uint32_t delay_ms, void *arg)
{
    (void)arg;
    osal_sleep_ms(delay_ms);
}

/** @brief Drain all currently available USART1 bytes into the AT parser. */
static void transport_w800_poll_uart(void *arg)
{
    transport_w800_tcp_t *transport = (transport_w800_tcp_t *)arg;
    uint8_t bytes[TRANSPORT_W800_RX_CHUNK_BYTES];
    uint32_t length;

    if(transport == NULL)
    {
        return;
    }
    do
    {
        length = 0U;
        if(bsp_uart_try_read(BOARD_UART_WIFI, bytes, sizeof(bytes), &length) !=
           BSP_STATUS_OK)
        {
            return;
        }
        if(length != 0U)
        {
            at_session_input(&transport->session, bytes, length);
        }
    } while(length != 0U);
}

/** @brief Read a bounded number of socket bytes without crossing one ADU boundary. */
static bsp_status_t transport_w800_receive_bytes(transport_w800_tcp_t *transport,
                                                 uint16_t requested)
{
    uint16_t actual = 0U;

    if(requested == 0U)
    {
        return BSP_STATUS_OK;
    }
    if(!at_module_recv_socket_id(&transport->module, transport->socket_id,
                                 &transport->receive_adu[transport->receive_length],
                                 requested, &actual))
    {
        return BSP_STATUS_IO_ERROR;
    }
    if(actual > requested)
    {
        return BSP_STATUS_OVERFLOW;
    }
    transport->receive_length = (uint16_t)(transport->receive_length + actual);
    return actual == 0U ? BSP_STATUS_NOT_READY : BSP_STATUS_OK;
}

/** @brief Implement transport_w800_tcp_init() as documented by its interface. */
bsp_status_t transport_w800_tcp_init(transport_w800_tcp_t *transport,
                                     const transport_w800_tcp_config_t *config)
{
    bsp_uart_config_t uart_config;
    w800_config_t driver_config;
    at_wifi_config_t wifi_config;
    at_socket_config_t socket_config;
    bsp_status_t status;

    if((transport == NULL) || (config == NULL) || (config->ssid == NULL) ||
       (config->password == NULL) || (config->local_port == 0U) ||
       (config->uart_baud_rate == 0U) ||
       (config->role > TRANSPORT_W800_TCP_SERVER) ||
       ((config->role == TRANSPORT_W800_TCP_CLIENT) &&
        ((config->remote_host == NULL) || (config->remote_port == 0U))))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(transport->is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    memset(transport, 0, sizeof(*transport));
    transport->socket_id = -1;
    transport->listener_socket_id = -1;
    transport->role = config->role;
    uart_config = (bsp_uart_config_t)
    {
        .baud_rate = config->uart_baud_rate,
        .receive_chunk_bytes = TRANSPORT_W800_RX_CHUNK_BYTES,
        .data_bits = 8U,
        .parity = BSP_UART_PARITY_NONE,
        .stop_bits = 1U,
        .rx_mode = BSP_UART_RX_MODE_IT,
        .tx_mode = BSP_UART_TX_MODE_POLLING
    };
    status = bsp_uart_init(BOARD_UART_WIFI, &uart_config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    driver_config = (w800_config_t)
    {
        .boot_mode = W800_BOOT_NORMAL,
        .reset_hold_ms = 100U,
        .boot_settle_ms = 3000U
    };
    status = w800_init(&driver_config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }

    at_session_init(&transport->session, transport_w800_at_write, transport,
                    transport_w800_now_ms, transport,
                    transport_w800_sleep_ms, transport);
    at_session_set_poll_callback(&transport->session,
                                 transport_w800_poll_uart, transport);
    at_module_init(&transport->module, &transport->session,
                   &g_at_module_w800, transport);

    wifi_config = (at_wifi_config_t)
    {
        .ssid = config->ssid,
        .password = config->password
    };
    if(!at_module_connect_network(&transport->module, &wifi_config))
    {
        return BSP_STATUS_TIMEOUT;
    }

    if(config->role == TRANSPORT_W800_TCP_SERVER)
    {
        if(!at_module_w800_open_server(&transport->module, config->local_port,
                                       config->server_idle_timeout_s,
                                       &transport->listener_socket_id))
        {
            return BSP_STATUS_IO_ERROR;
        }
        transport->is_initialized = true;
        return BSP_STATUS_OK;
    }

    socket_config = (at_socket_config_t)
    {
        .host = config->remote_host,
        .port = config->remote_port,
        .local_port = config->local_port
    };
    if(!at_module_open_socket_id(&transport->module, &socket_config,
                                 &transport->socket_id))
    {
        return BSP_STATUS_IO_ERROR;
    }
    transport->is_connected = true;
    transport->is_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Accept one W800 server child socket when the listener has a client. */
static bsp_status_t transport_w800_tcp_accept(transport_w800_tcp_t *transport)
{
    int client_socket;

    if(transport->role != TRANSPORT_W800_TCP_SERVER)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(!at_module_w800_accept_client(&transport->module,
                                     transport->listener_socket_id,
                                     &client_socket))
    {
        return BSP_STATUS_NOT_READY;
    }
    transport->socket_id = client_socket;
    transport->is_connected = true;
    return BSP_STATUS_OK;
}

/** @brief Implement complete-ADU transmission over the W800 client socket. */
bsp_status_t transport_w800_tcp_send_adu(transport_w800_tcp_t *transport,
                                         const uint8_t *adu,
                                         size_t length)
{
    uint16_t actual = 0U;

    if((transport == NULL) || (adu == NULL) || (length < 7U) ||
       (length > LD_MODBUS_TCP_MAX_ADU_LENGTH))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!transport->is_connected &&
       (transport_w800_tcp_accept(transport) != BSP_STATUS_OK))
    {
        return BSP_STATUS_NOT_READY;
    }
    if(!at_module_send_socket_id(&transport->module, transport->socket_id,
                                 adu, (uint16_t)length, &actual))
    {
        return BSP_STATUS_IO_ERROR;
    }
    return actual == length ? BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Implement one-ADU-at-a-time MBAP stream assembly. */
bsp_status_t transport_w800_tcp_try_read_adu(transport_w800_tcp_t *transport,
                                             uint8_t *adu,
                                             size_t capacity,
                                             size_t *length)
{
    bsp_status_t status;

    if((transport == NULL) || (adu == NULL) || (length == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *length = 0U;
    if(!transport->is_connected &&
       (transport_w800_tcp_accept(transport) != BSP_STATUS_OK))
    {
        return BSP_STATUS_NOT_READY;
    }

    if(transport->receive_length < 6U)
    {
        status = transport_w800_receive_bytes(
            transport, (uint16_t)(6U - transport->receive_length));
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
    }
    if(transport->expected_length == 0U)
    {
        const uint16_t mbap_length =
            (uint16_t)(((uint16_t)transport->receive_adu[4] << 8U) |
                       transport->receive_adu[5]);
        const uint32_t total = 6U + mbap_length;

        if((mbap_length < 2U) || (total > LD_MODBUS_TCP_MAX_ADU_LENGTH))
        {
            transport->receive_length = 0U;
            return BSP_STATUS_IO_ERROR;
        }
        transport->expected_length = (uint16_t)total;
    }
    if(transport->receive_length < transport->expected_length)
    {
        status = transport_w800_receive_bytes(
            transport, (uint16_t)(transport->expected_length -
                                  transport->receive_length));
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
    }
    if(capacity < transport->expected_length)
    {
        return BSP_STATUS_OVERFLOW;
    }

    memcpy(adu, transport->receive_adu, transport->expected_length);
    *length = transport->expected_length;
    transport->receive_length = 0U;
    transport->expected_length = 0U;
    return BSP_STATUS_OK;
}

/** @brief Implement outbound W800 socket closure. */
bsp_status_t transport_w800_tcp_close(transport_w800_tcp_t *transport)
{
    if(transport == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!transport->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(transport->is_connected &&
       !at_module_close_socket_id(&transport->module, transport->socket_id))
    {
        return BSP_STATUS_IO_ERROR;
    }
    transport->socket_id = -1;
    transport->is_connected = false;
    transport->receive_length = 0U;
    transport->expected_length = 0U;
    if((transport->listener_socket_id >= 0) &&
       !at_module_close_socket_id(&transport->module,
                                  transport->listener_socket_id))
    {
        return BSP_STATUS_IO_ERROR;
    }
    transport->listener_socket_id = -1;
    transport->is_initialized = false;
    return BSP_STATUS_OK;
}
