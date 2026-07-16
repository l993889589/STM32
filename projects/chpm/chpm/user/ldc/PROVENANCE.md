# CHPM LDC provenance and integration contract

CHPM consumes LDC 2.0.2 from `third_party/ldc`, pinned to the canonical GitHub
repository `https://github.com/l993889589/ldc.git` at commit
`d795674b47a760f02e8f253c1530b41d2d83c22f`.

The reusable library remains unmodified. Board and ThreadX policy lives only in
`dwin_ldc_channel.c`: it supplies the PRIMASK lock hooks, static storage, the
single producer/single consumer ownership rule, the 20 ms DWIN idle policy and
ThreadX notification objects.

CHPM creates exactly one LDC instance. DWIN is the only enabled private UART
byte stream that benefits from general framing. USBX data goes directly to its
application parser, application messages use ThreadX primitives, debug output
is not an RX command channel, and Modbus RTU uses the dedicated
`ld_modbus_rtu_framer` without passing through LDC.

Integration invariants:

- no dynamic allocation and no LDC-owned thread, timer or global registry;
- `ldc_rx_write()` must accept the entire DMA segment transactionally;
- UART IDLE calls `ldc_rx_idle()` only after the complete segment succeeds;
- the application-owned 1 ms service tick may commit an open DWIN frame after
  20 ms of silence, while holding the same ISR/task lock as the RX producer;
- `LDC_FULL_REJECT_NEW` exposes backpressure without silently evicting an
  unread DWIN frame;
- the DWIN owner thread is the only consumer and calls `ldc_frame_read()`;
- requesters wait for an ACK event published by that owner and never consume
  the LDC queue directly;
- UART errors abort only the current incomplete frame.

The dependency commit must be advanced deliberately and followed by the LDC
host suite, CHPM host suite, static validation and a compile-only Keil rebuild.
