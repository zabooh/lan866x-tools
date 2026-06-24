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

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "lan866x_cli.h"
#include "rcp.h"

/* SD multicast group the SOME/IP stack joins / sends FindService to. The stack
 * references this symbol from the application (each host tool defines it too). */
uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static bool s_rcp_inited = false;

static void cmd_discovery(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    uint8_t n, i;
    (void)pCmdIO; (void)argc; (void)argv;

    if (!s_rcp_inited) {
        SYS_CONSOLE_PRINT("[lan866x] SOME/IP client not started yet (interface up?)\r\n");
        return;
    }

    n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    SYS_CONSOLE_PRINT("[lan866x] endpoints discovered = %u\r\n", (unsigned)n);
    for (i = 0u; i < n; i++) {
        SYS_CONSOLE_PRINT("  #%u  %u.%u.%u.%u:%u  service=0x%04X inst=0x%04X avail=%d\r\n",
            (unsigned)i,
            eps[i].ip[0], eps[i].ip[1], eps[i].ip[2], eps[i].ip[3],
            (unsigned)eps[i].port, (unsigned)eps[i].serviceId,
            (unsigned)eps[i].instanceId, (int)eps[i].available);
    }
    if (n == 0u) {
        SYS_CONSOLE_PRINT("  (none yet - SD runs in background, retry in a moment)\r\n");
    }
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
