/*
 * env.c - persistent network config ("environment") on the Emulated EEPROM.
 *
 * The bridge ships identical firmware on every board with static network defaults
 * compiled into configuration.h. This layer keeps a versioned, CRC-protected copy
 * of the per-interface IP/mask/gateway/DNS in the Emulated EEPROM:
 *
 *   - On boot ENV_Init() reads the record; if it is missing/blank/corrupt (e.g. a
 *     freshly flashed board) it seeds the EEPROM from the compiled defaults.
 *   - env_apply() pushes the loaded config into the TCP/IP stack at runtime
 *     (TCPIP_STACK_NetAddressSet/...), so we never touch the MCC-generated
 *     TCPIP_HOSTS_CONFIGURATION.
 *   - CLI: showenv / setenv <key> <ip> / saveenv / readenv / resetenv.
 *
 * Defaults live in code (not a pre-baked EEPROM image): the emulated-EEPROM library
 * owns the on-flash format and formats a blank region on first init, so seeding from
 * the configuration.h values is the robust approach. "Change the build default" =
 * change configuration.h. No C++.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "definitions.h"
#include "configuration.h"                                   /* TCPIP_NETWORK_DEFAULT_* */
#include "config/default/library/tcpip/tcpip.h"              /* IPV4_ADDR, TCPIP_STACK_*, TCPIP_Helper_* */
#include "config/default/system/console/sys_console.h"
#include "system/command/sys_command.h"
#include "config/default/library/emulated_eeprom/emulated_eeprom.h"
#include "app.h"                                             /* APP_ApplyPlca() */
#include "env.h"

#define ENV_MAGIC    0x4C414E45u   /* 'LANE' */
#define ENV_VERSION  3u            /* v2 added PLCA, v3 added MACs; an older record reads invalid -> re-seed */
#define ENV_IF_CNT   2             /* [0] = eth0 (LAN865x/T1S), [1] = eth1 (GMAC/100BASE-T) */
#define ENV_EE_OFFSET 0u           /* byte offset of the record in the emulated EEPROM */

/* SAME54 128-bit device serial number, word 0 (least-significant word). The lowest
 * 3 bytes seed the eth0 MAC so every board is unique with one firmware image. */
#define SAME54_SERIAL_WORD0  (*(volatile uint32_t *)0x008061FCu)
static const uint8_t ENV_OUI[3] = { 0x00u, 0x04u, 0x25u };   /* Microchip OUI (matches the config default) */

/* Fixed-size record. Layout is 4-byte aligned throughout (the 2x6-byte mac block is
 * 12 bytes) so there is no padding and the crc32 is stable. ip/mask/gw/dns are
 * IPV4_ADDR.Val (network byte order); mac[i] are 6-byte MACs; plca_* are the eth0
 * PLCA node id/count. crc32 covers all bytes before it. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t ip[ENV_IF_CNT];
    uint32_t mask[ENV_IF_CNT];
    uint32_t gw[ENV_IF_CNT];
    uint32_t dns[ENV_IF_CNT];
    uint8_t  mac[ENV_IF_CNT][6];   /* eth0 = OUI+serial, eth1 = eth0 with low byte +1 */
    uint32_t plca_id;              /* eth0 PLCA node id    (0 = coordinator)         */
    uint32_t plca_cnt;             /* eth0 PLCA node count (PLCA_CTRL1 NODE_CNT)      */
    uint32_t crc32;
} env_t;

/* Derive the per-board default MACs from the SAME54 serial: eth0 = OUI + serial[2..0],
 * eth1 = eth0 with the lowest byte +1. */
static void env_derive_mac(uint8_t m0[6], uint8_t m1[6])
{
    uint32_t s = SAME54_SERIAL_WORD0;
    m0[0] = ENV_OUI[0]; m0[1] = ENV_OUI[1]; m0[2] = ENV_OUI[2];
    m0[3] = (uint8_t)(s >> 16); m0[4] = (uint8_t)(s >> 8); m0[5] = (uint8_t)s;
    memcpy(m1, m0, 6);
    m1[5] = (uint8_t)(m0[5] + 1u);
}

static env_t s_env;

/* The bridge's interfaces are reachable under these names (matches app.c). */
static const char *const ENV_IF[ENV_IF_CNT] = { "eth0", "eth1" };

/* --- CRC32 (IEEE 802.3, poly 0xEDB88320) over len bytes ------------------------- */
static uint32_t env_crc32(const void *p, size_t len)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i; int k;
    for (i = 0; i < len; i++) {
        crc ^= b[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}
static uint32_t env_calc_crc(const env_t *e)
{
    return env_crc32(e, offsetof(env_t, crc32));
}

/* --- defaults straight from configuration.h ------------------------------------- */
static void env_load_defaults(env_t *e)
{
    static const char *const dip[ENV_IF_CNT]   = { TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX0, TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX1 };
    static const char *const dmask[ENV_IF_CNT] = { TCPIP_NETWORK_DEFAULT_IP_MASK_IDX0,    TCPIP_NETWORK_DEFAULT_IP_MASK_IDX1 };
    static const char *const dgw[ENV_IF_CNT]   = { TCPIP_NETWORK_DEFAULT_GATEWAY_IDX0,    TCPIP_NETWORK_DEFAULT_GATEWAY_IDX1 };
    static const char *const ddns[ENV_IF_CNT]  = { TCPIP_NETWORK_DEFAULT_DNS_IDX0,        TCPIP_NETWORK_DEFAULT_DNS_IDX1 };
    int i; IPV4_ADDR a;
    memset(e, 0, sizeof *e);
    e->magic = ENV_MAGIC;
    e->version = ENV_VERSION;
    for (i = 0; i < ENV_IF_CNT; i++) {
        a.Val = 0; (void)TCPIP_Helper_StringToIPAddress(dip[i],   &a); e->ip[i]   = a.Val;
        a.Val = 0; (void)TCPIP_Helper_StringToIPAddress(dmask[i], &a); e->mask[i] = a.Val;
        a.Val = 0; (void)TCPIP_Helper_StringToIPAddress(dgw[i],   &a); e->gw[i]   = a.Val;
        a.Val = 0; (void)TCPIP_Helper_StringToIPAddress(ddns[i],  &a); e->dns[i]  = a.Val;
    }
    env_derive_mac(e->mac[0], e->mac[1]);
    e->plca_id  = (uint32_t)DRV_LAN865X_PLCA_NODE_ID_IDX0;
    e->plca_cnt = (uint32_t)DRV_LAN865X_PLCA_NODE_COUNT_IDX0;
    e->crc32 = env_calc_crc(e);
}

/* Format env MAC for interface (0/1) as "XX:XX:XX:XX:XX:XX" into buf (>=18 bytes). */
void env_mac_str(int iface, char *buf)
{
    const uint8_t *m;
    if (iface < 0 || iface >= ENV_IF_CNT) { buf[0] = '\0'; return; }
    m = s_env.mac[iface];
    (void)snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Parse "XX:XX:XX:XX:XX:XX" into out[6]; true on success. */
static bool env_parse_mac(const char *s, uint8_t out[6])
{
    unsigned v[6]; int i, n;
    n = sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    if (n != 6) return false;
    for (i = 0; i < 6; i++) { if (v[i] > 0xFFu) return false; out[i] = (uint8_t)v[i]; }
    return true;
}

/* --- EEPROM read/write ---------------------------------------------------------- */
static bool env_save(void)
{
    s_env.magic = ENV_MAGIC;
    s_env.version = ENV_VERSION;
    s_env.crc32 = env_calc_crc(&s_env);
    if (EMU_EEPROM_BufferWrite(ENV_EE_OFFSET, (const uint8_t *)&s_env, (uint16_t)sizeof s_env) != EMU_EEPROM_STATUS_OK)
        return false;
    return EMU_EEPROM_PageBufferCommit() == EMU_EEPROM_STATUS_OK;
}

/* Read a record from the EEPROM into *out; true only if magic+version+crc check out. */
static bool env_read_valid(env_t *out)
{
    if (EMU_EEPROM_BufferRead(ENV_EE_OFFSET, (uint8_t *)out, (uint16_t)sizeof *out) != EMU_EEPROM_STATUS_OK)
        return false;
    return out->magic == ENV_MAGIC && out->version == ENV_VERSION && out->crc32 == env_calc_crc(out);
}

/* --- apply to the running stack ------------------------------------------------- */
void env_apply(void)
{
    int i;
    for (i = 0; i < ENV_IF_CNT; i++) {
        TCPIP_NET_HANDLE nh = TCPIP_STACK_NetHandleGet(ENV_IF[i]);
        IPV4_ADDR ip, mask, gw, dns;
        if (nh == NULL)
            continue;
        ip.Val = s_env.ip[i]; mask.Val = s_env.mask[i]; gw.Val = s_env.gw[i]; dns.Val = s_env.dns[i];
        (void)TCPIP_STACK_NetAddressSet(nh, &ip, &mask, true);
        (void)TCPIP_STACK_NetAddressGatewaySet(nh, &gw);
        if (dns.Val != 0u)
            (void)TCPIP_STACK_NetAddressDnsPrimarySet(nh, &dns);
    }
    /* PLCA on eth0 (LAN865x) - queued via the app's LAN state machine. */
    APP_ApplyPlca((uint8_t)s_env.plca_id, (uint8_t)s_env.plca_cnt);
}

uint8_t env_plca_id(void)  { return (uint8_t)s_env.plca_id;  }
uint8_t env_plca_cnt(void) { return (uint8_t)s_env.plca_cnt; }

/* --- CLI ------------------------------------------------------------------------ */
static void pr_addr(const char *label, uint32_t val)
{
    char b[20]; IPV4_ADDR a; a.Val = val;
    (void)TCPIP_Helper_IPAddressToString(&a, b, sizeof b);
    SYS_CONSOLE_PRINT("%s%s", label, b);
}

static void cmd_showenv(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    int i; (void)pCmdIO; (void)argc; (void)argv;
    SYS_CONSOLE_PRINT("env (RAM shadow):\r\n");
    for (i = 0; i < ENV_IF_CNT; i++) {
        SYS_CONSOLE_PRINT("  eth%d  ", i);
        pr_addr("ip ",     s_env.ip[i]);
        pr_addr("  mask ", s_env.mask[i]);
        pr_addr("  gw ",   s_env.gw[i]);
        pr_addr("  dns ",  s_env.dns[i]);
        SYS_CONSOLE_PRINT("\r\n");
    }
    {
        char mb[18];
        env_mac_str(0, mb); SYS_CONSOLE_PRINT("  eth0  mac %s\r\n", mb);
        env_mac_str(1, mb); SYS_CONSOLE_PRINT("  eth1  mac %s  (applied at boot)\r\n", mb);
    }
    SYS_CONSOLE_PRINT("  plca  id %lu  count %lu  (eth0/T1S)\r\n",
                      (unsigned long)s_env.plca_id, (unsigned long)s_env.plca_cnt);
    SYS_CONSOLE_PRINT("  (saveenv = persist+apply, readenv = reload, resetenv = defaults)\r\n");
}

static uint32_t *env_field(const char *key)
{
    if (!strcmp(key, "ip0"))   return &s_env.ip[0];
    if (!strcmp(key, "mask0")) return &s_env.mask[0];
    if (!strcmp(key, "gw0"))   return &s_env.gw[0];
    if (!strcmp(key, "dns0"))  return &s_env.dns[0];
    if (!strcmp(key, "ip1"))   return &s_env.ip[1];
    if (!strcmp(key, "mask1")) return &s_env.mask[1];
    if (!strcmp(key, "gw1"))   return &s_env.gw[1];
    if (!strcmp(key, "dns1"))  return &s_env.dns[1];
    return NULL;
}

static void cmd_setenv(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint32_t *fld; IPV4_ADDR a; (void)pCmdIO;
    if (argc < 3) {
        SYS_CONSOLE_PRINT("usage: setenv <key> <val>\r\n"
                          "  IP keys:   ip0/mask0/gw0/dns0, ip1/mask1/gw1/dns1  (dotted-quad)\r\n"
                          "  MAC keys:  mac0, mac1  (XX:XX:XX:XX:XX:XX; applies after reset)\r\n"
                          "  PLCA keys: plca_id (0..254), plca_cnt (1..255)\r\n");
        return;
    }
    /* MAC keys (applied on next reset - the stack binds the MAC at init) */
    if (!strcmp(argv[1], "mac0") || !strcmp(argv[1], "mac1")) {
        int idx = (argv[1][3] == '1') ? 1 : 0;
        uint8_t m[6];
        if (!env_parse_mac(argv[2], m)) {
            SYS_CONSOLE_PRINT("setenv: bad MAC '%s' (use XX:XX:XX:XX:XX:XX)\r\n", argv[2]);
            return;
        }
        memcpy(s_env.mac[idx], m, 6);
        SYS_CONSOLE_PRINT("setenv: %s = %s (RAM only; 'saveenv' to persist; MAC applies after reset)\r\n",
                          argv[1], argv[2]);
        return;
    }
    /* numeric PLCA keys */
    if (!strcmp(argv[1], "plca_id") || !strcmp(argv[1], "plca_cnt")) {
        unsigned long v = strtoul(argv[2], NULL, 0);
        if (!strcmp(argv[1], "plca_id")) {
            if (v > 254u) { SYS_CONSOLE_PRINT("setenv: plca_id range 0..254\r\n"); return; }
            s_env.plca_id = (uint32_t)v;
        } else {
            if (v < 1u || v > 255u) { SYS_CONSOLE_PRINT("setenv: plca_cnt range 1..255\r\n"); return; }
            s_env.plca_cnt = (uint32_t)v;
        }
        SYS_CONSOLE_PRINT("setenv: %s = %lu (RAM only; 'saveenv' to persist)\r\n", argv[1], v);
        return;
    }
    fld = env_field(argv[1]);
    if (fld == NULL) { SYS_CONSOLE_PRINT("setenv: unknown key '%s'\r\n", argv[1]); return; }
    a.Val = 0;
    if (!TCPIP_Helper_StringToIPAddress(argv[2], &a)) { SYS_CONSOLE_PRINT("setenv: bad IP '%s'\r\n", argv[2]); return; }
    *fld = a.Val;
    SYS_CONSOLE_PRINT("setenv: %s = %s (RAM only; 'saveenv' to persist)\r\n", argv[1], argv[2]);
}

static void cmd_saveenv(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    (void)pCmdIO; (void)argc; (void)argv;
    if (env_save()) {
        env_apply();
        SYS_CONSOLE_PRINT("saveenv: persisted to EEPROM and applied "
                          "(an IP change drops the current connection).\r\n");
    } else {
        SYS_CONSOLE_PRINT("saveenv: EEPROM write FAILED.\r\n");
    }
}

static void cmd_readenv(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    env_t tmp; (void)pCmdIO; (void)argc; (void)argv;
    if (env_read_valid(&tmp)) {
        s_env = tmp;
        env_apply();
        SYS_CONSOLE_PRINT("readenv: reloaded from EEPROM and applied.\r\n");
    } else {
        SYS_CONSOLE_PRINT("readenv: no valid config in EEPROM.\r\n");
    }
}

static void cmd_resetenv(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    (void)pCmdIO; (void)argc; (void)argv;
    env_load_defaults(&s_env);
    if (env_save()) {
        env_apply();
        SYS_CONSOLE_PRINT("resetenv: restored compiled defaults, persisted and applied.\r\n");
    } else {
        SYS_CONSOLE_PRINT("resetenv: defaults applied but EEPROM write FAILED.\r\n");
    }
}

static const SYS_CMD_DESCRIPTOR env_cmd_tbl[] = {
    {"showenv",  (SYS_CMD_FNC)cmd_showenv,  ": show the current network config (RAM shadow)"},
    {"setenv",   (SYS_CMD_FNC)cmd_setenv,   ": setenv <key> <val>  (ip0../dns1, mac0/mac1, plca_id, plca_cnt)"},
    {"saveenv",  (SYS_CMD_FNC)cmd_saveenv,  ": persist config to EEPROM and apply it live"},
    {"readenv",  (SYS_CMD_FNC)cmd_readenv,  ": reload config from EEPROM and apply (discards unsaved edits)"},
    {"resetenv", (SYS_CMD_FNC)cmd_resetenv, ": reset to compiled defaults, persist and apply"},
};

void ENV_Init(void)
{
    env_t tmp;
    /* A freshly flashed board has a blank EEPROM region -> BAD_FORMAT; format it once. */
    if (EMU_EEPROM_StatusGet() == EMU_EEPROM_STATUS_ERR_BAD_FORMAT)
        (void)EMU_EEPROM_FormatMemory();

    if (env_read_valid(&tmp)) {
        s_env = tmp;                       /* valid persisted config */
    } else {
        env_load_defaults(&s_env);         /* first boot / blank / corrupt */
        (void)env_save();                  /* seed the EEPROM from the compiled defaults */
    }
    SYS_CMD_ADDGRP(env_cmd_tbl, (int)(sizeof env_cmd_tbl / sizeof *env_cmd_tbl),
                   "env", ": persistent network config (saveenv/readenv/showenv)");
}
