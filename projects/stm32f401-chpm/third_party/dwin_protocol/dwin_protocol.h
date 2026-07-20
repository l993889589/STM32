/**
 * @file dwin_protocol.h
 * @brief Pure-C DWIN DGUS frame construction, CRC, and acknowledgement parsing.
 */

#ifndef DWIN_PROTOCOL_H
#define DWIN_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DWIN_PROTOCOL_VERSION_MAJOR     1U
#define DWIN_PROTOCOL_VERSION_MINOR     0U
#define DWIN_PROTOCOL_VERSION_PATCH     0U
#define DWIN_PROTOCOL_MAX_FRAME_BYTES   258U
#define DWIN_PROTOCOL_MAX_WRITE_PAYLOAD 250U

/** @brief Classification of the fixed DWIN 0x82 write acknowledgement. */
typedef enum
{
    DWIN_PROTOCOL_ACK_NONE = 0,
    DWIN_PROTOCOL_ACK_PLAIN,
    DWIN_PROTOCOL_ACK_CRC
} dwin_protocol_ack_t;

/**
 * @brief Calculate the CRC-16/Modbus value used by CRC-enabled DWIN frames.
 * @param data Input bytes; may be NULL only when length is zero.
 * @param length Number of bytes to include.
 * @return Standard CRC integer; encode its low byte first on the DWIN wire.
 */
uint16_t dwin_protocol_crc16(const uint8_t *data, size_t length);

/**
 * @brief Build one CRC-enabled DWIN 0x82 variable-write frame.
 * @param address DWIN variable address in host byte order.
 * @param payload Payload bytes; may be NULL only when payload_length is zero.
 * @param payload_length Payload length in bytes.
 * @param output Caller-owned destination buffer.
 * @param output_capacity Destination capacity in bytes.
 * @param output_length Receives the complete frame length.
 * @return True when the complete frame was encoded.
 */
bool dwin_protocol_build_write(uint16_t address,
                               const uint8_t *payload,
                               uint16_t payload_length,
                               uint8_t *output,
                               uint16_t output_capacity,
                               uint16_t *output_length);

/**
 * @brief Classify a complete fixed DWIN 0x82 write acknowledgement.
 * @param frame Complete candidate frame.
 * @param length Candidate length in bytes.
 * @return Plain, CRC-enabled, or no acknowledgement match.
 */
dwin_protocol_ack_t dwin_protocol_classify_ack(const uint8_t *frame,
                                                uint16_t length);

#endif /* DWIN_PROTOCOL_H */
