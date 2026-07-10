/* Production OTA P-256 public key coordinates compiled into protected Boot. */
#ifndef BOOT_TRUST_ANCHOR_H
#define BOOT_TRUST_ANCHOR_H

#include <stdint.h>

#define BOOT_TRUST_ANCHOR_COORDINATE_SIZE 32U

extern const uint8_t boot_trust_anchor_public_x[BOOT_TRUST_ANCHOR_COORDINATE_SIZE];
extern const uint8_t boot_trust_anchor_public_y[BOOT_TRUST_ANCHOR_COORDINATE_SIZE];

#endif /* BOOT_TRUST_ANCHOR_H */
