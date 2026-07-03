#ifndef APP_QSPI_LOADER_H
#define APP_QSPI_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "app_serial_ldc.h"

#define APP_QSPI_LOADER_BASE        0x00700000UL
#define APP_QSPI_LOADER_SIZE        0x00100000UL
#define APP_QSPI_LOADER_MAX_PAYLOAD 224U

bool app_qspi_loader_handle_frame(app_serial_ldc_t *serial,
                                  const uint8_t *frame,
                                  uint16_t length);

#endif /* APP_QSPI_LOADER_H */
