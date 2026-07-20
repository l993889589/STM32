/**
 * @file dwin_tx.h
 * @brief Single ThreadX transmit owner for all CHPM DWIN traffic.
 *
 * This service serializes every DWIN write, coalesces dynamic values by VP,
 * and owns the reliable periodic buzzer schedule. The peer's identical fixed
 * ACK is treated as link activity by the receive owner, not as proof that one
 * particular write succeeded.
 */

#ifndef DWIN_TX_H
#define DWIN_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "dwin_tx_policy.h"

/** @brief Admission result returned by non-blocking DWIN submissions. */
typedef enum
{
    DWIN_TX_STATUS_OK = 0,
    DWIN_TX_STATUS_INVALID_ARGUMENT,
    DWIN_TX_STATUS_NOT_READY,
    DWIN_TX_STATUS_FULL,
    DWIN_TX_STATUS_INTERNAL_ERROR
} dwin_tx_status_t;

/** @brief Runtime transport diagnostics. */
typedef struct
{
    uint32_t transactions_started;
    uint32_t transactions_succeeded;
    uint32_t uart_failures;
    uint32_t buzzer_successes;
    uint32_t reset_requests;
    dwin_tx_policy_diagnostics_t policy;
} dwin_tx_diagnostics_t;

/**
 * @brief Initialize and start the only DWIN transmit owner thread.
 * @return DWIN_TX_STATUS_OK on success or an explicit initialization error.
 * @note Call after DWIN LDC/UART initialization from ThreadX task context.
 */
dwin_tx_status_t dwin_tx_init(void);

/**
 * @brief Submit an ordered raw DWIN event frame.
 * @param frame Complete on-wire frame copied before return.
 * @param length Frame length in bytes.
 * @param retry_limit Number of retries after the first failed transaction.
 * @return Admission status; the call does not wait for the DWIN ACK.
 * @note Task-context only; not ISR-safe.
 */
dwin_tx_status_t dwin_tx_submit_raw_event(const uint8_t *frame,
                                          uint16_t length,
                                          uint8_t retry_limit);

/**
 * @brief Submit one replaceable raw dynamic frame.
 * @param key Stable coalescing key, normally the DWIN VP address.
 * @param frame Complete on-wire frame copied before return.
 * @param length Frame length in bytes.
 * @return Admission status; a pending frame with the same key is overwritten.
 * @note Task-context only; not ISR-safe.
 */
dwin_tx_status_t dwin_tx_submit_raw_latest(uint16_t key,
                                           const uint8_t *frame,
                                           uint16_t length);

/**
 * @brief Submit the screen-reset frame and reset the MCU after UART delivery.
 * @param frame Complete on-wire DWIN reset frame.
 * @param length Frame length in bytes.
 * @param retry_limit Number of UART retries before abandoning the request.
 * @return Admission status.
 * @note Reset frames do not wait for ACK because the screen may reset first.
 */
dwin_tx_status_t dwin_tx_submit_reset(const uint8_t *frame,
                                      uint16_t length,
                                      uint8_t retry_limit);

/**
 * @brief Build and submit an ordered CRC-enabled DWIN 0x82 write.
 * @param address DWIN variable address.
 * @param payload Payload copied before return.
 * @param length Payload length in bytes.
 * @param retry_limit Number of retries after the first failure.
 * @return Admission or argument status.
 */
dwin_tx_status_t dwin_tx_submit_write_event(uint16_t address,
                                            const uint8_t *payload,
                                            uint16_t length,
                                            uint8_t retry_limit);

/**
 * @brief Build and submit a latest-value CRC-enabled DWIN 0x82 write.
 * @param address DWIN variable address and coalescing key.
 * @param payload Payload copied before return.
 * @param length Payload length in bytes.
 * @return Admission or argument status.
 */
dwin_tx_status_t dwin_tx_submit_write_latest(uint16_t address,
                                             const uint8_t *payload,
                                             uint16_t length);

/**
 * @brief Enable or disable the reliable periodic DWIN buzzer schedule.
 * @param active True to beep immediately and repeat every five seconds.
 * @return Status indicating whether the owner accepted the state update.
 * @note A failed buzzer transaction is retried until success or deactivation.
 */
dwin_tx_status_t dwin_tx_set_buzzer(bool active);

/**
 * @brief Copy coherent DWIN transmit diagnostics.
 * @param diagnostics Destination owned by the caller.
 * @return DWIN_TX_STATUS_OK on success.
 */
dwin_tx_status_t dwin_tx_get_diagnostics(
    dwin_tx_diagnostics_t *diagnostics);

#endif /* DWIN_TX_H */
