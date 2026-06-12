#ifndef LDC_PROTO_DISPATCHER_H
#define LDC_PROTO_DISPATCHER_H

#include <stdint.h>

typedef enum
{
    LDC_PROTO_UNHANDLED = 0,
    LDC_PROTO_HANDLED = 1
} ldc_proto_result_t;

/* A protocol handler receives one complete frame from LDC. */
typedef ldc_proto_result_t (*ldc_proto_handler_t)(const uint8_t *data, uint32_t len, void *arg);

typedef struct
{
    ldc_proto_handler_t handler;
    void *arg;
} ldc_proto_entry_t;

typedef struct
{
    ldc_proto_entry_t *entries;
    uint8_t capacity;
    uint8_t count;
} ldc_proto_dispatcher_t;

/* Initialize with a caller-owned static entry array. No heap allocation is used. */
void ldc_proto_dispatcher_init(ldc_proto_dispatcher_t *dispatcher,
                               ldc_proto_entry_t *entries,
                               uint8_t capacity);

/* Handlers are tried in registration order until one returns LDC_PROTO_HANDLED. */
int ldc_proto_dispatcher_register(ldc_proto_dispatcher_t *dispatcher,
                                  ldc_proto_handler_t handler,
                                  void *arg);
int ldc_proto_dispatcher_dispatch(ldc_proto_dispatcher_t *dispatcher,
                                  const uint8_t *data,
                                  uint32_t len);

#endif
