# UART4 / LDC Porting Review

Date: 2026-06-28

This document records the issues introduced or exposed during today's UART4,
USBX, shell and LDC bring-up work. The goal is to keep the fault history clear
so the next debug pass does not repeat the same mistakes.

## Target Requirement

The current target is:

- Enable UART4.
- Enable UART4 RX DMA.
- Receive UART4 data through interrupt/DMA idle events.
- Feed received UART4 bytes into LDC.
- Echo exactly the received bytes back through UART4.
- UART4 echo transmit path must not block RX interrupt handling.

## Current Unresolved Symptom

Observed after the latest change:

- LED does not blink.
- UART4 receives no visible echo response.

Current likely causes to check first:

- `App_ThreadX_Init()` calls `app_uart4_echo_init()` before creating the LED thread. If `app_uart4_echo_init()` returns anything other than `TX_SUCCESS`, `App_ThreadX_Init()` returns `TX_START_ERROR`, and the LED thread is never created.
- `user/bsp/bsp.c` currently has `bsp_led_off()` and `bsp_led_init()` commented out. The application LED thread toggles `LED_R_GPIO_Port/LED_R_Pin` directly, so GPIO may still be initialized by CubeMX, but BSP LED state is no longer a reliable bring-up indicator.
- UART4 echo TX depends on `HAL_UART_TxCpltCallback()`. If TX interrupt is not firing, the echo thread will block waiting on `g_uart4_tx_done`.

Recommended next debug step:

- Temporarily create the LED thread before `app_uart4_echo_init()` or ignore the echo init return once, so LED can prove the scheduler is alive.
- Add a simple fault marker for each failure return inside `app_uart4_echo_init()`.
- Verify `UART4_IRQHandler()` is reached on RX and TX complete.

## Mistake 1: USB PCD Was Initialized Before USBX Was Ready

What happened:

- `main()` originally called `MX_USB_OTG_FS_PCD_Init()` before ThreadX entered `tx_application_define()`.
- That enabled USB interrupts before USBX device stack and DCD state were fully initialized.
- If USB was plugged in during boot, a USB reset interrupt could reach USBX callbacks while USBX internal objects were not ready.

Why it was wrong:

- USBX STM32 callbacks access `_ux_system_slave` and DCD endpoint structures.
- Those structures must be initialized before USB hardware interrupts are allowed to call into USBX.

Fix applied:

- Removed early `MX_USB_OTG_FS_PCD_Init()` from `Core/Src/main.c`.
- Called `ux_dcd_stm32_initialize()` during `MX_USBX_Device_Init()`.
- Moved `MX_USB_OTG_FS_PCD_Init()` and `HAL_PCD_Start()` into the USBX device thread after USBX/DCD registration.

Lesson:

- For USBX, do not start or interrupt-enable PCD before USBX stack and DCD are registered.

## Mistake 2: USBX Device Stack Was Not Actually Started

What happened:

- `MX_USBX_Device_Init()` in `AZURE_RTOS/App/app_azure_rtos.c` was commented out.
- Only low-level PCD init existed, so CDC ACM class registration never happened.

Why it was wrong:

- PCD init only configures hardware.
- USBX device stack initialization and CDC ACM class registration are separate and required.

Fix applied:

- Re-enabled `MX_USBX_Device_Init()` in `tx_application_define()`.

Lesson:

- USB CDC needs both hardware PCD initialization and USBX class stack registration.

## Mistake 3: Shell, Debug UART and LDC Port Concepts Were Mixed

What happened:

- `APP_LDC_PORT_DEBUG` was added as if UART4 shell/debug was an LDC framed data port.
- `app_config.h` accumulated mixed names such as debug UART baudrate, LDC frame size and shell settings.

Why it was wrong:

- LDC should describe framed data channels.
- Shell is a character-interactive service and does not need LDC framing unless explicitly required.
- Mixing these concepts makes binding fragile and creates confusing names across files.

Fix applied:

- Removed `APP_LDC_PORT_DEBUG`.
- Added dedicated `APP_SHELL_*` settings for shell use.
- Later, when the requirement changed from shell to UART4 echo-through-LDC, added a specific `APP_LDC_PORT_UART4_ECHO`.
- Changed the LDC config table to designated initializers, for example:
  - `[APP_LDC_PORT_USB_CDC] = {...}`
  - `[APP_LDC_PORT_UART4_ECHO] = {...}`

Lesson:

- Use LDC port IDs only for real framed data paths.
- Use separate app-level config for shell/debug/user interaction services.

## Mistake 4: BSP UART Binding Was Cleared After Being Set

What happened:

- `bsp_init()` previously bound UART4 and then called `bsp_uart_init()`.
- `bsp_uart_init()` clears all UART descriptors.

Why it was wrong:

- The bind was erased immediately.
- Later RX/TX operations could fail because the UART handle was no longer associated with the BSP port.

Fix applied:

- Changed `bsp_init()` order to call `bsp_uart_init()` first, then `bsp_uart_bind()`.

Lesson:

- Descriptor reset must happen before peripheral binding.

## Mistake 5: UART RX Was Started Too Early for ThreadX Queue Use

What happened:

- UART RX was started during initialization while the callback path used ThreadX queues.

Why it was risky:

- If RX interrupt fires before scheduler and queue usage are safe for the intended path, behavior becomes hard to reason about.

Fix applied:

- Moved UART RX start into the service thread entry for shell/echo versions.

Lesson:

- If RX ISR posts to RTOS objects, start RX after those objects exist and the runtime path is ready.

## Mistake 6: Non-Blocking Echo Was Implemented Too Late

What happened:

- Earlier shell output used blocking UART writes.
- The latest requirement explicitly needs receiving UART4 data and echoing it without blocking interrupt receive handling.

Why it was wrong for this requirement:

- Blocking transmit inside RX or command paths can stall timing-sensitive receive work.

Fix applied:

- Added `bsp_uart_write_it()` using `HAL_UART_Transmit_IT()`.
- Added `bsp_uart_register_tx_callback()`.
- Added `HAL_UART_TxCpltCallback()` to notify a semaphore.
- Added `app_uart4_echo.c`:
  - RX callback writes data to LDC.
  - RX callback queues raw bytes into a TX queue.
  - TX thread sends chunks through interrupt-driven UART transmit.

Lesson:

- RX interrupt/DMA callbacks should do minimal work: store/feed/queue, then return.
- Actual transmit work belongs in a task/thread or DMA/IT completion pipeline.

## Mistake 7: Build Success Was Treated As Bring-Up Success

What happened:

- Multiple changes were verified by Keil build only.
- The project compiled, but the board later showed no LED blink and no UART echo.

Why it was wrong:

- A firmware build only proves syntax/link correctness.
- It does not prove task creation, interrupt routing, DMA callbacks, or runtime state.

Fix needed:

- Add runtime bring-up markers:
  - LED thread starts regardless of optional service init failures.
  - Separate error blink codes for `app_uart4_echo_init()` failure points.
  - Counters for RX callback, TX complete callback, LDC writes and queue drops.

Lesson:

- For embedded bring-up, every init stage needs a visible runtime proof.

## Current Code Areas To Inspect Next

- `Core/Src/app_threadx.c`
  - LED thread is created only after `app_uart4_echo_init()` succeeds.
- `user/app/app_uart4_echo.c`
  - Check which init call fails if LED is not created.
  - Check whether `HAL_UART_TxCpltCallback()` is reached.
- `user/bsp/bsp.c`
  - LED BSP init is commented out.
  - UART4 is bound with DMA enabled.
- `user/bsp/bsp_uart.c`
  - RX path uses `HAL_UARTEx_ReceiveToIdle_DMA()`.
  - TX path uses `HAL_UART_Transmit_IT()`.
- `Core/Src/stm32h7xx_it.c`
  - Confirm `UART4_IRQHandler()` and `DMA1_Stream0_IRQHandler()` are firing.

## Proposed Immediate Fix Plan

1. Move LED thread creation before `app_uart4_echo_init()`.
2. Restore or verify LED GPIO/BSP initialization.
3. Add an init result variable or blink code for each failure in `app_uart4_echo_init()`.
4. Add counters:
   - UART4 RX events.
   - LDC write bytes.
   - TX queued bytes.
   - TX complete events.
   - TX queue drops.
5. Rebuild and test UART4 with USB disconnected first.

