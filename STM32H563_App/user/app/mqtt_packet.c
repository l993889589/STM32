#include "mqtt_packet.h"

#include <string.h>

static uint32_t mqtt_write_remaining_length(uint8_t *out, uint32_t value)
{
    uint32_t used = 0U;

    do
    {
        uint8_t encoded = (uint8_t)(value % 128U);
        value /= 128U;
        if(value > 0U)
            encoded |= 0x80U;
        out[used++] = encoded;
    } while(value > 0U && used < 4U);

    return used;
}

static uint16_t mqtt_write_utf8(uint8_t *out, const char *text)
{
    uint16_t len = (uint16_t)strlen(text);

    out[0] = (uint8_t)(len >> 8);
    out[1] = (uint8_t)(len & 0xFFU);
    memcpy(&out[2], text, len);

    return (uint16_t)(len + 2U);
}

uint16_t mqtt_build_connect(uint8_t *out, uint16_t max_len, const char *client_id, uint16_t keepalive_s)
{
    uint32_t remaining;
    uint32_t pos = 0U;

    if(!out || !client_id)
        return 0U;

    remaining = 10U + 2U + (uint32_t)strlen(client_id);
    if(max_len < (remaining + 5U))
        return 0U;

    out[pos++] = 0x10U;
    pos += mqtt_write_remaining_length(&out[pos], remaining);
    pos += mqtt_write_utf8(&out[pos], "MQTT");
    out[pos++] = 0x04U;
    out[pos++] = 0x02U;
    out[pos++] = (uint8_t)(keepalive_s >> 8);
    out[pos++] = (uint8_t)keepalive_s;
    pos += mqtt_write_utf8(&out[pos], client_id);

    return (uint16_t)pos;
}

uint16_t mqtt_build_publish(uint8_t *out, uint16_t max_len, const char *topic, const char *payload)
{
    uint32_t topic_len;
    uint32_t payload_len;
    uint32_t remaining;
    uint32_t pos = 0U;

    if(!out || !topic || !payload)
        return 0U;

    topic_len = (uint32_t)strlen(topic);
    payload_len = (uint32_t)strlen(payload);
    remaining = 2U + topic_len + payload_len;

    if(max_len < (remaining + 5U))
        return 0U;

    out[pos++] = 0x30U;
    pos += mqtt_write_remaining_length(&out[pos], remaining);
    pos += mqtt_write_utf8(&out[pos], topic);
    memcpy(&out[pos], payload, payload_len);
    pos += payload_len;

    return (uint16_t)pos;
}

uint16_t mqtt_build_subscribe(uint8_t *out, uint16_t max_len, uint16_t packet_id, const char *topic, uint8_t qos)
{
    uint32_t topic_len;
    uint32_t remaining;
    uint32_t pos = 0U;

    if(!out || !topic || qos > 2U)
        return 0U;

    topic_len = (uint32_t)strlen(topic);
    remaining = 2U + 2U + topic_len + 1U;
    if(max_len < (remaining + 5U))
        return 0U;

    out[pos++] = 0x82U;
    pos += mqtt_write_remaining_length(&out[pos], remaining);
    out[pos++] = (uint8_t)(packet_id >> 8);
    out[pos++] = (uint8_t)(packet_id & 0xFFU);
    pos += mqtt_write_utf8(&out[pos], topic);
    out[pos++] = qos;

    return (uint16_t)pos;
}