# app_serial_ldc usage examples

`app_serial_ldc` only binds a UART transport to one LDC endpoint. Direct UART
send is still available through `app_serial_ldc_send_blocking()` or the lower
level `bsp_uart_write()`.

## UART4 echo

```c
static app_serial_ldc_t uart4_shell;
static ULONG uart4_tx_queue[512];
static UCHAR uart4_stack[1024];
static uint8_t uart4_rx_dma[128];
static uint8_t uart4_tx_chunk[64];
static uint8_t uart4_ldc_ring[513];
static ldc_packet_t uart4_packets[8];

const app_ldc_port_config_t *ldc_cfg =
    app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);

app_serial_ldc_config_t cfg = {
    .name = "uart4-shell",
    .uart_port = BSP_UART4,
    .ldc_config = ldc_cfg,
    .rx_dma = uart4_rx_dma,
    .rx_dma_size = sizeof(uart4_rx_dma),
    .tx_queue_storage = uart4_tx_queue,
    .tx_queue_depth = 512,
    .tx_chunk = uart4_tx_chunk,
    .tx_chunk_size = sizeof(uart4_tx_chunk),
    .thread_stack = uart4_stack,
    .thread_stack_size = sizeof(uart4_stack),
    .thread_priority = 14,
    .ldc_ring = uart4_ldc_ring,
    .ldc_ring_size = sizeof(uart4_ldc_ring),
    .ldc_packets = uart4_packets,
    .ldc_packet_count = 8,
    .flags = APP_SERIAL_LDC_FLAG_ECHO_RX,
};

(void)app_serial_ldc_init(&uart4_shell, &cfg);
```

## Direct send

```c
static const uint8_t ok[] = "OK\r\n";

(void)app_serial_ldc_send_blocking(&uart4_shell, ok, sizeof(ok) - 1U, 100U);
```

## Read LDC frames in another thread

```c
uint8_t frame[256];
int len = app_serial_ldc_read_frame(&uart4_shell, frame, sizeof(frame));
if(len > 0)
{
    /* process one complete LDC frame */
}
```

## Frame callback

```c
static uint8_t frame_buf[256];

static void on_frame(app_serial_ldc_t *serial,
                     const uint8_t *frame,
                     uint16_t length,
                     void *arg)
{
    (void)serial;
    (void)arg;
    (void)app_serial_ldc_send_async(&uart4_shell, frame, length);
}

cfg.frame_buffer = frame_buf;
cfg.frame_buffer_size = sizeof(frame_buf);
cfg.frame_cb = on_frame;
```
