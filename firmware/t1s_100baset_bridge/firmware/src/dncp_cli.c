/*
 * dncp_cli.c - DNCP family of the bridge CLI: mirrors the host tools
 *   dncpmon (passive monitor) and dncpdisc (active discovery). DNCP is raw UDP
 *   on 65526/65527 (NOT SOME/IP), so this uses plat_udp_* directly. Registered
 *   as the "dncp" SYS_CMD group; type the name directly. Both are bounded and
 *   abortable with Ctrl-C / 'q'. No C++.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "plat.h"
#include "lan866x_cli.h"

static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch; int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint64_t be64(const uint8_t *p) { uint64_t v = 0; int i; for (i = 0; i < 8; ++i) v = (v << 8) | p[i]; return v; }
static const char *dncp_type(uint8_t t)
{ switch (t) { case 0: return "REQUEST"; case 1: return "REQUEST_NO_RESPONSE"; case 2: return "RESPONSE";
               case 3: return "ERROR"; case 4: return "NOTIFICATION"; default: return "?"; } }
static const char *dncp_id(uint16_t id)
{ switch (id) { case 0x100: return "Registry"; case 0x200: return "Announce"; case 0x300: return "StartTDMeasurement";
               case 0x301: return "GetTDMeasurementResult"; case 0x400: return "StoreSettings"; case 0x401: return "Activate";
               default: return "?"; } }
static const char *dncp_state(uint8_t s)
{ switch (s) { case 0: return "Undefined"; case 1: return "Unconfigured"; case 2: return "Pre-Configured";
               case 3: return "Configured"; default: return "?"; } }

/* ===================== dncpmon (passive) ================================== */
static void dncp_decode(const uint8_t *b, uint16_t len)
{
    uint16_t id, cnt; uint8_t ver, type; int j;
    if (len < 16u) return;
    ver = b[10]; type = b[11]; id = be16(b + 12); cnt = be16(b + 14);
    SYS_CONSOLE_PRINT("\r\n[DNCP] %s / %s (proto v%u, cnt %u, %u B)\r\n",
                      dncp_id(id), dncp_type(type), (unsigned)ver, (unsigned)cnt, (unsigned)len);
    SYS_CONSOLE_PRINT("  Header MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n", b[4], b[5], b[6], b[7], b[8], b[9]);
    if (id == 0x200u && len >= 56u) {           /* Announce */
        uint8_t slots = b[55];
        SYS_CONSOLE_PRINT("  Node MAC:   %02X:%02X:%02X:%02X:%02X:%02X\r\n", b[18], b[19], b[20], b[21], b[22], b[23]);
        SYS_CONSOLE_PRINT("  Device-ID:  0x%016llX\r\n", (unsigned long long)be64(b + 24));
        SYS_CONSOLE_PRINT("  IPv4:       %u.%u.%u.%u\r\n", b[48], b[49], b[50], b[51]);
        SYS_CONSOLE_PRINT("  State:      %s   Persistency: %s\r\n", dncp_state(b[53]), b[52] ? "Persistent" : "Non-Persistent");
        SYS_CONSOLE_PRINT("  PLCA slots: %u  IDs:", (unsigned)slots);
        for (j = 0; j < slots && (56 + j) < (int)len; ++j) SYS_CONSOLE_PRINT(" %u", b[56 + j]);
        SYS_CONSOLE_PRINT("\r\n");
    } else if (id == 0x100u && len >= 19u) {    /* Registry */
        SYS_CONSOLE_PRINT("  EnumChannel:%u NodeCount:%u Entries:%u\r\n", b[16], b[17], b[18]);
    }
}
static void mon_rx(plat_udp_t *s, const uint8_t ip[4], uint16_t port,
                   const uint8_t *buf, uint16_t len, void *tag)
{ (void)s; (void)ip; (void)port; (void)tag; dncp_decode(buf, len); }

static void cmd_dncpmon(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    plat_udp_t *s1, *s2; uint16_t p1 = 65526u, p2 = 65527u;
    uint32_t secs = 30u, start, endt;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;
    if (argc >= 2) secs = (uint32_t)strtoul(argv[1], NULL, 10);
    if (secs < 1u) secs = 1u; if (secs > 600u) secs = 600u;

    s1 = plat_udp_open(&p1, mon_rx, NULL);
    s2 = plat_udp_open(&p2, mon_rx, NULL);
    if (!s1 || !s2) { SYS_CONSOLE_PRINT("[dncpmon] socket open failed\r\n"); if (s1) plat_udp_close(s1); if (s2) plat_udp_close(s2); return; }

    SYS_CONSOLE_PRINT("DNCP monitor on UDP 65526/65527 for %u s (passive; 'q' to stop)...\r\n", (unsigned)secs);
    start = plat_now_ms(); endt = start + secs * 1000u;
    while (!aborted && (int32_t)(plat_now_ms() - endt) < 0) {
        plat_udp_poll();
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(5);
    }
    plat_udp_close(s1); plat_udp_close(s2);
    SYS_CONSOLE_PRINT("\r\nDNCP monitor stopped.\r\n");
}

/* ===================== dncpdisc (active) ================================== */
static uint8_t  g_macs[64][6];
static int      g_nNodes;
static void disc_rx(plat_udp_t *s, const uint8_t ip[4], uint16_t port,
                    const uint8_t *b, uint16_t len, void *tag)
{
    int j; uint8_t slots;
    (void)s; (void)ip; (void)port; (void)tag;
    if (len < 56u || be16(b + 12) != 0x0200u) return;     /* Announce only */
    for (j = 0; j < g_nNodes; ++j) if (memcmp(g_macs[j], b + 18, 6) == 0) return;  /* dedup */
    if (g_nNodes >= 64) return;
    memcpy(g_macs[g_nNodes], b + 18, 6);
    slots = b[55];
    SYS_CONSOLE_PRINT("\r\n==== DNCP node #%d ====\r\n", g_nNodes);
    SYS_CONSOLE_PRINT("  MAC:        %02X:%02X:%02X:%02X:%02X:%02X\r\n", b[18], b[19], b[20], b[21], b[22], b[23]);
    SYS_CONSOLE_PRINT("  Device-ID:  0x%016llX\r\n", (unsigned long long)be64(b + 24));
    SYS_CONSOLE_PRINT("  IPv4:       %u.%u.%u.%u\r\n", b[48], b[49], b[50], b[51]);
    SYS_CONSOLE_PRINT("  State:      %s   Persistency: %s\r\n", dncp_state(b[53]), b[52] ? "Persistent" : "Non-Persistent");
    SYS_CONSOLE_PRINT("  PLCA ids:   %u total:", (unsigned)slots);
    for (j = 0; j < slots && (56 + j) < (int)len; ++j) SYS_CONSOLE_PRINT(" %u", b[56 + j]);
    SYS_CONSOLE_PRINT("\r\n");
    g_nNodes++;
}

static void cmd_dncpdisc(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    plat_udp_t *s; uint16_t port = 65526u; uint8_t mc[4] = { 224, 0, 0, 1 };
    uint8_t reg[19]; uint16_t cnt = 1u, length; uint8_t channel = 11u;
    int rounds = 3, timeoutS = 4, r, t;
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    int aborted = 0;
    (void)pCmdIO;
    if (argc >= 2) channel  = (uint8_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) rounds   = (int)strtoul(argv[2], NULL, 10);
    if (argc >= 4) timeoutS = (int)strtoul(argv[3], NULL, 10);
    if (rounds < 1) rounds = 1; if (rounds > 20) rounds = 20;
    if (timeoutS < 1) timeoutS = 1; if (timeoutS > 30) timeoutS = 30;

    g_nNodes = 0;
    s = plat_udp_open(&port, disc_rx, NULL);     /* bound to 65526: RX announces + TX registry */
    if (!s) { SYS_CONSOLE_PRINT("[dncpdisc] socket open failed\r\n"); return; }

    SYS_CONSOLE_PRINT("Active DNCP discovery (channel %u) - broadcasting Registry, collecting Announces.\r\n", (unsigned)channel);
    SYS_CONSOLE_PRINT("  (read-only: assigns nothing; use only when no other DNCP server is active)\r\n");

    memset(reg, 0, sizeof(reg));
    memset(reg + 4, 0xFF, 6);                    /* MacAddress = broadcast */
    reg[10] = 1;                                 /* ProtocolVersion */
    reg[11] = 1;                                 /* REQUEST_NO_RESPONSE */
    reg[12] = 0x01; reg[13] = 0x00;              /* MessageId 0x0100 Registry */
    reg[16] = channel; reg[17] = 0; reg[18] = 0; /* EnumChannel, NodeCount=0, Entries=0 */
    length = (uint16_t)(sizeof(reg) - 2u); reg[0] = (uint8_t)(length >> 8); reg[1] = (uint8_t)length;

    for (r = 0; r < rounds && !aborted; ++r) {
        reg[14] = (uint8_t)(cnt >> 8); reg[15] = (uint8_t)cnt; cnt++;
        plat_udp_send(s, mc, 65527u, reg, sizeof(reg));
        for (t = 0; t < 10 && !aborted; ++t) {
            plat_udp_poll();
            if (chk_abort(con)) aborted = 1;
            plat_sleep_ms(100);
        }
    }
    for (t = 0; t < timeoutS * 10 && !aborted; ++t) {
        plat_udp_poll();
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(100);
    }

    SYS_CONSOLE_PRINT("\r\n%d DNCP node(s) found.\r\n", g_nNodes);
    if (g_nNodes == 0) SYS_CONSOLE_PRINT("Note: DNCP must be enabled on the nodes, else none answer.\r\n");
    plat_udp_close(s);
}

static const SYS_CMD_DESCRIPTOR dncp_cmd_tbl[] = {
    {"dncpmon",  (SYS_CMD_FNC) cmd_dncpmon,  ": passive DNCP monitor UDP 65526/65527 (dncpmon [secs])"},
    {"dncpdisc", (SYS_CMD_FNC) cmd_dncpdisc, ": active DNCP discovery (dncpdisc [channel] [rounds] [timeoutS])"},
};

void DNCP_CLI_Init(void)
{
    SYS_CMD_ADDGRP(dncp_cmd_tbl, sizeof(dncp_cmd_tbl) / sizeof(*dncp_cmd_tbl), "dncp", ": DNCP discovery");
}
