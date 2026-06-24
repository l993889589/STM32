#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include <stdint.h>

uint16_t mqtt_build_connect(uint8_t *out, uint16_t max_len, const char *client_id, uint16_t keepalive_s);
uint16_t mqtt_build_publish(uint8_t *out, uint16_t max_len, const char *topic, const char *payload);

#endif /* MQTT_PACKET_H */
