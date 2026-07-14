#ifndef NX_USER_H
#define NX_USER_H

/* First hardware milestone is IPv4 ARP and ICMP. IPv6 can be enabled later. */
#define NX_DISABLE_IPV6

/* NetX timeouts are expressed in ThreadX timer ticks. */
#define NX_IP_PERIODIC_RATE TX_TIMER_TICKS_PER_SECOND

/* This board currently runs DHCP only on the single Ethernet interface. */
#define NX_DHCP_CLIENT_MAX_RECORDS 1U

#endif
