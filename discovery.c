/*
 * discovery.c  -  List reachable LAN866x endpoints and their full status
 *                 (GetStatus 0x1002 + GetNetworkStatus 0x1600). Pure C.
 *
 * Usage:
 *   lan866x-discovery                 list all endpoints with full status
 *   lan866x-discovery --wait 8        longer discovery window
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include "rcp.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static const char *arb_str(uint8_t a)
{
    switch (a) { case 0: return "CSMA/CD"; case 1: return "PLCA"; case 2: return "PLCA no fallback"; }
    return "unknown";
}

static const char *ep_type(const char *chip)
{
    if (!chip) return "Endpoint";
    if (strstr(chip, "LAN8660")) return "Control Endpoint";
    if (strstr(chip, "LAN8661")) return "Lighting Endpoint (LED/Display)";
    if (strstr(chip, "LAN8662")) return "Audio Endpoint";
    return "Endpoint";
}

static void print_status(int idx, const rcp_endpoint_t *e)
{
    GetStatusReply_t st;
    GetNetworkStatusReply_t ns;
    ReturnCode_t rc;

    printf("\n========================================================\n");
    printf("Endpoint #%d  -  %u.%u.%u.%u:%u  (instance 0x%04X, available=%d)\n",
           idx, e->ip[0], e->ip[1], e->ip[2], e->ip[3], e->port, e->instanceId, e->available);
    printf("========================================================\n");

    memset(&st, 0, sizeof(st));
    rc = rcp_get_status(&st);
    if (rc == RT_OK) {
        unsigned long long up = st.UpTime / 1000000000ULL;
        printf("  Uptime:             %lluh %llum %llus\n", up/3600ULL, (up%3600ULL)/60ULL, up%60ULL);
        printf("  Application:        %s\n", st.ActiveApplication);
        printf("  Chip Identifier:    %s   -> %s\n", st.ChipIdentifier, ep_type((const char *)st.ChipIdentifier));
        printf("  Main Version:       %s\n", st.MainApplicationVersion);
        printf("  Root Version:       %s\n", st.RootApplicationVersion);
        printf("  Bootloader Version: %s\n", st.BootApplicationVersion);
        printf("  COMO Version:       0x%08X\n", st.ComoVersion);
        printf("  Service Version:    0x%08X\n", st.ServiceVersion);
        printf("  Keys Version:       %s\n", st.KeysVersion);
        printf("  StartupInformation: 0x%016llX (Security Mode %u)\n",
               (unsigned long long)st.StartupInformation,
               (unsigned)((st.StartupInformation >> 9) & 0x3u));
    } else {
        printf("  GetStatus failed (rc=%d)\n", rc);
    }

    memset(&ns, 0, sizeof(ns));
    rc = rcp_get_network_status(&ns);
    if (rc == RT_OK) {
        uint64_t mac = ns.EndpointAddress;
        printf("  MAC:                %02X:%02X:%02X:%02X:%02X:%02X\n",
               (unsigned)((mac>>40)&0xFF),(unsigned)((mac>>32)&0xFF),(unsigned)((mac>>24)&0xFF),
               (unsigned)((mac>>16)&0xFF),(unsigned)((mac>>8)&0xFF),(unsigned)(mac&0xFF));
        printf("  IPv4:               %u.%u.%u.%u\n",
               (unsigned)((ns.EndpointIpV4Address>>24)&0xFF),(unsigned)((ns.EndpointIpV4Address>>16)&0xFF),
               (unsigned)((ns.EndpointIpV4Address>>8)&0xFF),(unsigned)(ns.EndpointIpV4Address&0xFF));
        printf("  Endpoint Status:    %s\n", ns.EndpointStatus==1?"Link-Up":ns.EndpointStatus==2?"Link-Down":"unknown");
        printf("  OASPI Status:       %s\n", ns.OaspiStatus==0?"Disabled":ns.OaspiStatus==1?"Link-Up":"Link-Down");
        printf("  Arbitration:        %s\n", arb_str(ns.ArbitrationMode));
        printf("  PLCA Node Id:       %u\n", ns.PLCANodeId);
    } else {
        printf("  GetNetworkStatus failed (rc=%d)\n", rc);
    }
}

int main(int argc, char **argv)
{
    int i, waitS = 5;
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint8_t n;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-discovery - list endpoints + full status (pure C)\n"
                   "  --wait <s>   discovery window (default 5)\n");
            return 0;
        } else if (!strcmp(argv[i], "--wait") && i + 1 < argc) waitS = atoi(argv[++i]);
    }

    printf("LAN866x discovery (pure C)\nSearching for endpoints (%d s) ...\n", waitS);
    if (!rcp_init(NULL)) { printf("rcp_init failed.\n"); return 1; }
    for (i = 0; i < waitS * 10; ++i) { rcp_poll(); Sleep(100); }

    n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    printf("\nDevices available = %u\n", n);
    if (n == 0) { printf("Nothing found. Check driver / NIC IP 192.168.0.x / bus / power / firewall.\n"); return 2; }

    for (i = 0; i < (int)n; ++i) {
        if (!rcp_select_endpoint((uint8_t)i)) continue;
        print_status(i, &eps[i]);
    }
    return 0;
}
