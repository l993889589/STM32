#ifndef APP_NETX_SERVICES_H
#define APP_NETX_SERVICES_H

#include "nx_api.h"

#define APP_NETX_TCP_ECHO_PORT 7U
#define APP_NETX_HTTP_PORT     80U

UINT app_netx_services_start(NX_IP *ip, NX_PACKET_POOL *packet_pool);

#endif
