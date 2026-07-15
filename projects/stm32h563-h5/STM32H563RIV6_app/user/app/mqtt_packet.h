#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdint.h>

uint16_t mqtt_build_connect(uint8_t *out, uint16_t max_len, const char *client_id, uint16_t keepalive_s);
uint16_t mqtt_build_publish(uint8_t *out, uint16_t max_len, const char *topic, const char *payload);
uint16_t mqtt_build_subscribe(uint8_t *out, uint16_t max_len, uint16_t packet_id, const char *topic, uint8_t qos);
uint16_t mqtt_build_subscribe_many(uint8_t *out,
                                   uint16_t max_len,
                                   uint16_t packet_id,
                                   const char *const *topics,
                                   uint8_t topic_count,
                                   uint8_t qos);

#endif /* MQTT_PACKET_H */
