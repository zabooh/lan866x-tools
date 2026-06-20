/*
 * plat.h  -  Narrow platform abstraction for the single-thread (bare-metal /
 *            superloop) port of the LAN866x toolset. No C++.
 *
 * This is the WHOLE porting surface. A new target provides exactly one file
 * (plat_<target>.c) implementing the functions below; everything above it -
 * the SOME/IP platform stub (someip_stub.c), rcp.c and the tools - is
 * platform-neutral and stays unchanged.
 *
 * Three responsibilities:
 *   1. plat_now_ms()                 - a millisecond time base
 *   2. non-blocking UDP + net        - open / send / poll-receive (+ multicast
 *                                      join and local-interface enumeration)
 *   3. plat_sleep_ms() / plat_yield  - cooperative wait between superloop ticks
 *
 * SINGLE-THREAD MODEL: there are no platform threads. Received datagrams are
 * delivered SYNCHRONOUSLY from plat_udp_poll(), which the application calls
 * once per superloop iteration. Because the RX callback runs on the same
 * (only) execution strand as the rest of the program, no locks, atomics or
 * volatile cross-thread state are needed anywhere above this layer.
 *
 * MCU/lwIP mapping (template for plat_lwip.c, see PORTING.md):
 *   plat_now_ms          -> xTaskGetTickCount()*portTICK_PERIOD_MS  (or systick)
 *   plat_udp_open        -> udp_new / udp_bind / udp_recv
 *   plat_udp_send        -> udp_sendto
 *   plat_udp_join_mcast  -> igmp_joingroup
 *   plat_udp_poll        -> drain the lwIP RX queue (RAW API: pump in the
 *                           superloop; received pbufs dispatched to the rx cb)
 *   plat_net_enum_ifaces -> iterate netif_list
 *   plat_sleep_ms/yield  -> vTaskDelay / taskYIELD  (or a busy wait on bare metal)
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>
#include <stdbool.h>

/* ===================== 1) time base ===================================== */
/* Monotonic milliseconds. Initial offset is don't-care; must only count up
 * (32-bit wrap is fine - all users compare deltas). */
uint32_t plat_now_ms(void);

/* ===================== 2) non-blocking UDP + net ======================= */
typedef struct plat_udp plat_udp_t;   /* opaque UDP socket handle */

/* RX callback. Invoked SYNCHRONOUSLY from plat_udp_poll() for each received
 * datagram. srcIp/srcPort are the remote peer; buf/len are valid only for the
 * duration of the call (copy if needed). */
typedef void (*plat_udp_rx_cb)(plat_udp_t *s, const uint8_t srcIp[4],
                               uint16_t srcPort, const uint8_t *buf,
                               uint16_t len, void *tag);

/* Open a non-blocking UDP socket bound to *port (INADDR_ANY). If *port is 0 an
 * ephemeral port is chosen and written back. rx (+ tag) receives datagrams via
 * plat_udp_poll(). Returns NULL on failure. */
plat_udp_t *plat_udp_open(uint16_t *port, plat_udp_rx_cb rx, void *tag);

/* Send one datagram. Returns true if it was handed to the stack. */
bool plat_udp_send(plat_udp_t *s, const uint8_t dstIp[4], uint16_t dstPort,
                   const uint8_t *buf, uint16_t len);

/* Join multicast group on the given local interface (and set it as the
 * outgoing interface for this socket). Idempotent. */
bool plat_udp_join_multicast(plat_udp_t *s, const uint8_t group[4],
                             const uint8_t localIf[4]);

/* Poll ALL open sockets once: drain every ready datagram and dispatch it to its
 * rx callback synchronously. NEVER blocks. Returns the number of datagrams
 * dispatched (0 if none were ready). Call once per superloop iteration. */
int plat_udp_poll(void);

/* Close and free a socket. */
void plat_udp_close(plat_udp_t *s);

/* Enumerate local IPv4 interfaces (for target-subnet match and multicast join).
 * cb is invoked once per usable interface with its IP and netmask. */
typedef void (*plat_if_cb)(const uint8_t ip[4], const uint8_t mask[4], void *tag);
void plat_net_enum_ifaces(plat_if_cb cb, void *tag);

/* ===================== 3) wait / yield ================================= */
/* Sleep approximately ms milliseconds (frame cadence / backoff between ticks). */
void plat_sleep_ms(uint32_t ms);

/* Give up the rest of the current time slice without a fixed delay. */
void plat_yield(void);

#endif /* PLAT_H */
