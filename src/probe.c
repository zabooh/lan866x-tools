/*
 * probe.c  -  Pure-C end-to-end demo on top of rcp.c / libsomeip.
 *
 * Proves the vanilla-C path on Windows: this tool, the RCP wrapper (rcp.c) and
 * the platform stub (someip_stub_win.c) are all C, linked with the C-only
 * SOME/IP core (libsomeip/src/*.c). No C++ client, no libstdc++.
 *
 * What it does:
 *   1. rcp_init() -> start SOME/IP + service discovery for service 0xFF10
 *   2. wait a few seconds and list every endpoint that offered the service
 *      (this exercises the SD receive path entirely in C)
 *   3. call GetStatus on the selected endpoint and show the raw response
 *      (this exercises the request/response round-trip entirely in C)
 *
 * Usage:
 *   lan866x-probe-c                 discover, then GetStatus on endpoint [0]
 *   lan866x-probe-c --ep 1
 *   lan866x-probe-c --ip 192.168.0.54
 *   lan866x-probe-c --wait 8        discovery window in seconds (default 5)
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "rcp.h"

/* SD multicast group, referenced by the platform stub and rcp.c. */
uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static void usage(void)
{
    printf("lan866x-probe-c - pure-C discovery + GetStatus demo\n\n"
           "USAGE:\n"
           "  lan866x-probe-c [--ep <index> | --ip <addr>] [--wait <seconds>]\n\n"
           "OPTIONS:\n"
           "  --ep <index>    target endpoint by index (default 0)\n"
           "  --ip <addr>     target endpoint by IPv4 address\n"
           "  --wait <s>      discovery window in seconds (default 5)\n"
           "  -h, --help      this help\n");
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, waitS = 5;
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--ep")   && i + 1 < argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ip")   && i + 1 < argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) waitS  = atoi(argv[++i]);
    }

    printf("lan866x-probe-c (pure C, no C++ runtime)\n");
    if (!rcp_init(NULL)) { printf("rcp_init failed.\n"); return 1; }

    printf("Searching for endpoints (%d s) ...\n", waitS);
    for (i = 0; i < waitS * 10; ++i) { rcp_poll(); Sleep(100); }

    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint8_t n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    if (n == 0) {
        printf("No endpoints found. (driver? NIC IP 192.168.0.x? bus/power? firewall?)\n");
        return 2;
    }

    printf("\nDevices available = %u\n", n);
    int sel = -1;
    for (i = 0; i < n; ++i) {
        char s[20];
        snprintf(s, sizeof(s), "%u.%u.%u.%u", eps[i].ip[0], eps[i].ip[1], eps[i].ip[2], eps[i].ip[3]);
        printf("  [%d] %s:%u  service 0x%04X  instance 0x%04X  %s\n",
               i, s, eps[i].port, eps[i].serviceId, eps[i].instanceId,
               eps[i].available ? "available" : "stopped");
        if ((wantIp && !strcmp(wantIp, s)) || (!wantIp && i == wantEp)) sel = i;
    }
    if (sel < 0) { printf("Target endpoint not found.\n"); return 2; }
    if (!rcp_select_endpoint((uint8_t)sel)) { printf("select failed.\n"); return 2; }

    printf("\nGetStatus on endpoint [%d] ...\n", sel);
    uint8_t rx[1440];
    uint16_t rxLen = sizeof(rx);
    if (rcp_get_status(rx, &rxLen)) {
        printf("  GetStatus OK - %u payload bytes\n", rxLen);
        uint16_t k;
        printf("  first bytes:");
        for (k = 0; k < rxLen && k < 32; ++k) printf(" %02X", rx[k]);
        printf("%s\n", rxLen > 32 ? " ..." : "");
        printf("\nRound-trip OK - request and response handled in pure C.\n");
        return 0;
    }
    printf("  GetStatus failed (no response / timeout).\n");
    return 3;
}
