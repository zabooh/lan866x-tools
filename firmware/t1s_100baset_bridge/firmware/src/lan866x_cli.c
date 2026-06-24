/*
 * lan866x_cli.c - LAN866x SOME/IP client commands for the bridge CLI.
 *
 * M1: background Service Discovery + a "discovery" command that lists the
 * endpoints seen on the T1S bus (mirrors lan866x-discovery.exe). SD runs
 * passively from rcp_poll() in the superloop; the command just prints the
 * current endpoint table. Blocking RCP method calls (GetStatus, etc.) come in
 * a later milestone - they must not be issued from a CLI handler because that
 * would starve SYS_Tasks() (the TCP/IP stack) on this single strand.
 *
 * RAM note: no large buffers here. The endpoint snapshot lives on the caller's
 * stack (RCP_MAX_ENDPOINTS * sizeof(rcp_endpoint_t), ~200 B).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "lan866x_cli.h"
#include "rcp.h"

/* SD multicast group the SOME/IP stack joins / sends FindService to. The stack
 * references this symbol from the application (each host tool defines it too). */
uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static bool s_rcp_inited = false;

/* --- mirrors lan866x-discovery.exe (discovery.c) --------------------------- */
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

    SYS_CONSOLE_PRINT("\r\n========================================================\r\n");
    SYS_CONSOLE_PRINT("Endpoint #%d  -  %u.%u.%u.%u:%u  (instance 0x%04X, available=%d)\r\n",
           idx, e->ip[0], e->ip[1], e->ip[2], e->ip[3], e->port, e->instanceId, e->available);
    SYS_CONSOLE_PRINT("========================================================\r\n");

    memset(&st, 0, sizeof(st));
    rc = rcp_get_status(&st);
    if (rc == RT_OK) {
        unsigned long long up = st.UpTime / 1000000000ULL;
        SYS_CONSOLE_PRINT("  Uptime:             %lluh %llum %llus\r\n", up/3600ULL, (up%3600ULL)/60ULL, up%60ULL);
        SYS_CONSOLE_PRINT("  Application:        %s\r\n", st.ActiveApplication);
        SYS_CONSOLE_PRINT("  Chip Identifier:    %s   -> %s\r\n", st.ChipIdentifier, ep_type((const char *)st.ChipIdentifier));
        SYS_CONSOLE_PRINT("  Main Version:       %s\r\n", st.MainApplicationVersion);
        SYS_CONSOLE_PRINT("  Root Version:       %s\r\n", st.RootApplicationVersion);
        SYS_CONSOLE_PRINT("  Bootloader Version: %s\r\n", st.BootApplicationVersion);
        SYS_CONSOLE_PRINT("  COMO Version:       0x%08X\r\n", st.ComoVersion);
        SYS_CONSOLE_PRINT("  Service Version:    0x%08X\r\n", st.ServiceVersion);
        SYS_CONSOLE_PRINT("  Keys Version:       %s\r\n", st.KeysVersion);
        SYS_CONSOLE_PRINT("  StartupInformation: 0x%016llX (Security Mode %u)\r\n",
               (unsigned long long)st.StartupInformation,
               (unsigned)((st.StartupInformation >> 9) & 0x3u));
    } else {
        SYS_CONSOLE_PRINT("  GetStatus failed (rc=%d)\r\n", rc);
    }

    memset(&ns, 0, sizeof(ns));
    rc = rcp_get_network_status(&ns);
    if (rc == RT_OK) {
        uint64_t mac = ns.EndpointAddress;
        SYS_CONSOLE_PRINT("  MAC:                %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               (unsigned)((mac>>40)&0xFF),(unsigned)((mac>>32)&0xFF),(unsigned)((mac>>24)&0xFF),
               (unsigned)((mac>>16)&0xFF),(unsigned)((mac>>8)&0xFF),(unsigned)(mac&0xFF));
        SYS_CONSOLE_PRINT("  IPv4:               %u.%u.%u.%u\r\n",
               (unsigned)((ns.EndpointIpV4Address>>24)&0xFF),(unsigned)((ns.EndpointIpV4Address>>16)&0xFF),
               (unsigned)((ns.EndpointIpV4Address>>8)&0xFF),(unsigned)(ns.EndpointIpV4Address&0xFF));
        SYS_CONSOLE_PRINT("  Endpoint Status:    %s\r\n", ns.EndpointStatus==1?"Link-Up":ns.EndpointStatus==2?"Link-Down":"unknown");
        SYS_CONSOLE_PRINT("  OASPI Status:       %s\r\n", ns.OaspiStatus==0?"Disabled":ns.OaspiStatus==1?"Link-Up":"Link-Down");
        SYS_CONSOLE_PRINT("  Arbitration:        %s\r\n", arb_str(ns.ArbitrationMode));
        SYS_CONSOLE_PRINT("  PLCA Node Id:       %u\r\n", ns.PLCANodeId);
    } else {
        SYS_CONSOLE_PRINT("  GetNetworkStatus failed (rc=%d)\r\n", rc);
    }
}

static void cmd_discovery(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint8_t n, i;
    (void)pCmdIO; (void)argc; (void)argv;

    if (!s_rcp_inited) {
        SYS_CONSOLE_PRINT("[lan866x] SOME/IP client not started yet (interface up?)\r\n");
        return;
    }

    /* Snappier than the 1500ms x3 host default: the T1S link is ~2 ms RTT. */
    rcp_set_timeout_ms(1000);
    rcp_set_retries(1);

    n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    SYS_CONSOLE_PRINT("\r\nDevices available = %u\r\n", (unsigned)n);
    if (n == 0u) {
        SYS_CONSOLE_PRINT("Nothing found (SD runs in background; retry shortly, check T1S/PLCA).\r\n");
    }
    for (i = 0u; i < n; i++) {
        if (!rcp_select_endpoint(i)) continue;
        print_status((int)i, &eps[i]);
    }

    rcp_set_timeout_ms(1500);   /* restore defaults */
    rcp_set_retries(3);
}

static const SYS_CMD_DESCRIPTOR lan866x_cmd_tbl[] = {
    {"discovery", (SYS_CMD_FNC) cmd_discovery, ": list LAN866x endpoints seen via SOME/IP SD"},
};

void LAN866X_CLI_Init(void)
{
    (void)SYS_CMD_ADDGRP(lan866x_cmd_tbl,
                         (int)(sizeof(lan866x_cmd_tbl) / sizeof(*lan866x_cmd_tbl)),
                         "lan866x", ": LAN866x RCP client commands");
}

void LAN866X_CLI_Task(void)
{
    if (!s_rcp_inited) {
        /* Wait until the T1S interface (eth0, index 0) has a valid IP, then
         * start the SOME/IP client once. rcp_init() enumerates interfaces,
         * joins the SD group and sends a FindService for service 0xFF10. */
        TCPIP_NET_HANDLE eth0 = TCPIP_STACK_IndexToNet(0);
        if (eth0 != 0 && TCPIP_STACK_NetIsUp(eth0) &&
            TCPIP_STACK_NetAddress(eth0) != 0u) {
            if (rcp_init(NULL)) {
                s_rcp_inited = true;
                SYS_CONSOLE_PRINT("[lan866x] SOME/IP client started (SD on UDP 30490)\r\n");
            }
        }
        return;
    }
    rcp_poll();
}
