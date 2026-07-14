#ifndef APP_NETX_H
#define APP_NETX_H

#include "nx_api.h"

#define APP_NETX_IP_ADDRESS   IP_ADDRESS(192, 168, 1, 50)
#define APP_NETX_NETWORK_MASK IP_ADDRESS(255, 255, 255, 0)

UINT app_netx_start(void);

#endif
