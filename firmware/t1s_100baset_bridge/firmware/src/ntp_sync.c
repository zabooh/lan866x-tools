/*
 * ntp_sync.c - software NTP-style time sync between the bridge firmware and a PC.
 *
 * The bridge keeps a free-running, high-resolution **NTP time counter** that
 * starts at boot (raw = SYS_TIME hardware counter, ns) plus a signed offset. A PC
 * tool (lan866x-ntpsync) opens this UDP service, runs an NTP t1/t2/t3/t4 exchange
 * to measure the clock offset, and then pushes a SET_OFFSET so the counter reads
 * the PC's wall-clock time. After that the firmware runs autonomously on its own
 * disciplined counter - usable to timestamp firmware events on the PC timebase.
 *
 * Resolution is bounded by the SAME54 / Harmony SYS_TIME counter; the actual
 * tick frequency (and hence the ns resolution) is reported by the "ntp" CLI.
 *
 * Wire protocol - UDP port 30491, all integers big-endian, signed 64-bit ns:
 *   0x01 REQUEST   PC->FW : [op][t1]                 (t1 = PC send time)
 *   0x02 REPLY     FW->PC : [op][t1][t2][t3]         (t2 = FW recv, t3 = FW send)
 *   0x03 SET_OFFSET PC->FW: [op][adjust][delay]       (FW: offset_ns += adjust; stores
 *                                                     the PC-measured round-trip delay)
 *   0x04 SET_ACK   FW->PC : [op][now]                (now = FW ntp time after set)
 *
 * The service socket is NOT pinned to an interface, so the PC reaches it directly
 * on the bridge (e.g. eth1 192.168.0.181:30491). No C++.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "definitions.h"
#include "config/default/library/tcpip/tcpip.h"
#include "config/default/system/console/sys_console.h"
#include "config/default/system/time/sys_time.h"
#include "system/command/sys_command.h"
#include "plat.h"            /* plat_now_ms()/plat_sleep_ms() - pump stack while watching */
#include "lan866x_cli.h"

#define NTP_PORT      30491u
#define OP_REQUEST    0x01u
#define OP_REPLY      0x02u
#define OP_SET_OFFSET 0x03u
#define OP_SET_ACK    0x04u

static UDP_SOCKET s_sock = INVALID_UDP_SOCKET;
static int64_t    s_offset_ns = 0;       /* added to the raw counter to align to PC time */
static int64_t    s_last_adjust_ns = 0;  /* last correction the PC applied               */
static int64_t    s_last_delay_ns = 0;   /* last round-trip delay the PC measured         */
static int        s_synced = 0;
static uint32_t   s_syncCount = 0;

/* Format a ns duration human-readable (ASCII, integer math, signed): ns / us / ms / s. */
static const char *fmt_dur(char *b, int n, int64_t ns)
{
    const char *sg = (ns < 0) ? "-" : "";
    uint64_t a = (ns < 0) ? (uint64_t)(-ns) : (uint64_t)ns;
    if (a < 1000ULL)              snprintf(b, n, "%s%llu ns", sg, (unsigned long long)a);
    else if (a < 1000000ULL)      snprintf(b, n, "%s%llu.%03llu us", sg, (unsigned long long)(a/1000ULL), (unsigned long long)(a%1000ULL));
    else if (a < 1000000000ULL)   snprintf(b, n, "%s%llu.%03llu ms", sg, (unsigned long long)(a/1000000ULL), (unsigned long long)((a%1000000ULL)/1000ULL));
    else                          snprintf(b, n, "%s%llu.%03llu s",  sg, (unsigned long long)(a/1000000000ULL), (unsigned long long)((a%1000000000ULL)/1000000ULL));
    return b;
}

/* Raw monotonic ns since boot from the SYS_TIME counter, overflow-safe:
 * ns = sec*1e9 + (frac_ticks*1e9)/freq  (avoids ticks*1e9 overflowing uint64). */
static uint64_t ntp_raw_ns(void)
{
    uint64_t freq = (uint64_t)SYS_TIME_FrequencyGet();
    uint64_t ticks, sec, frac;
    if (freq == 0u) return 0u;
    ticks = SYS_TIME_Counter64Get();
    sec   = ticks / freq;
    frac  = ticks % freq;
    return sec * 1000000000ULL + (frac * 1000000000ULL) / freq;
}

/* Disciplined NTP time in ns (PC-aligned once a SET_OFFSET has been applied). */
uint64_t ntp_now_ns(void)
{
    return (uint64_t)((int64_t)ntp_raw_ns() + s_offset_ns);
}

static void put64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0; int i;
    for (i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static void reply_to(const UDP_SOCKET_INFO *info, const uint8_t *buf, uint16_t len)
{
    IP_MULTI_ADDRESS dst;
    if (TCPIP_UDP_TxPutIsReady(s_sock, len) < len) return;
    dst.v4Add = info->sourceIPaddress.v4Add;
    if (!TCPIP_UDP_DestinationIPAddressSet(s_sock, IP_ADDRESS_TYPE_IPV4, &dst)) return;
    if (!TCPIP_UDP_DestinationPortSet(s_sock, info->remotePort)) return;
    if (TCPIP_UDP_ArrayPut(s_sock, buf, len) == len) TCPIP_UDP_Flush(s_sock);
}

/* abort key from the console RX ring (pumped via plat_sleep_ms): Ctrl-C / 'q'. */
static int chk_abort(SYS_CONSOLE_HANDLE con)
{
    char ch; int hit = 0;
    while (SYS_CONSOLE_Read(con, &ch, 1) > 0)
        if (ch == 0x03 || ch == 'q' || ch == 'Q') hit = 1;
    return hit;
}

static void ntp_print_status(void)
{
    uint64_t freq = (uint64_t)SYS_TIME_FrequencyGet();
    uint64_t up = ntp_raw_ns();
    uint64_t now = ntp_now_ns();
    char b1[40], b2[40], b3[40];
    SYS_CONSOLE_PRINT("NTP time counter:\r\n");
    SYS_CONSOLE_PRINT("  source     : SYS_TIME, %lu Hz  (resolution ~%lu ns/tick)\r\n",
                      (unsigned long)freq, (unsigned long)(freq ? (1000000000ULL / freq) : 0));
    SYS_CONSOLE_PRINT("  uptime     : %lu.%09lu s (raw)\r\n",
                      (unsigned long)(up / 1000000000ULL), (unsigned long)(up % 1000000000ULL));
    SYS_CONSOLE_PRINT("  offset     : %s\r\n", fmt_dur(b1, sizeof b1, s_offset_ns));
    SYS_CONSOLE_PRINT("  last delay : %s\r\n", s_synced ? fmt_dur(b2, sizeof b2, s_last_delay_ns) : "(n/a)");
    SYS_CONSOLE_PRINT("  last adjust: %s\r\n", s_synced ? fmt_dur(b3, sizeof b3, s_last_adjust_ns) : "(n/a)");
    SYS_CONSOLE_PRINT("  synced     : %s (%lu sync msg)\r\n", s_synced ? "YES" : "no", (unsigned long)s_syncCount);
    SYS_CONSOLE_PRINT("  NTP time   : %llu.%09llu s",
                      (unsigned long long)(now / 1000000000ULL), (unsigned long long)(now % 1000000000ULL));
    if (s_synced) SYS_CONSOLE_PRINT("  (PC-aligned, Unix epoch)");
    SYS_CONSOLE_PRINT("\r\n");
}

/* Continuous watch: one line per sync received from the PC (same format as the PC
 * tool), until 'q'/Ctrl-C (or the optional [secs] cap). The loop pumps NTP_Task()
 * so syncs are processed while this handler blocks, and plat_sleep_ms() pumps the
 * stack + console so bridging and the abort key keep working. */
static void ntp_watch(uint32_t secs)
{
    SYS_CONSOLE_HANDLE con = SYS_CONSOLE_HandleGet(SYS_CONSOLE_INDEX_0);
    uint32_t last = s_syncCount, start = plat_now_ms(), lastPrint = 0u;
    int aborted = 0;
    SYS_CONSOLE_PRINT("Watching NTP syncs (~1 line/s, latest sync; 'q' or Ctrl-C to stop)...\r\n");
    while (!aborted && (secs == 0u || (plat_now_ms() - start) < secs * 1000u)) {
        /* Service the NTP socket promptly so a request is not left sitting in the
         * UDP buffer (which would inflate the measured delay). EVERY sync is
         * processed; printing is throttled to ~1/s so the console I/O never
         * competes with the time exchange. */
        NTP_Task();
        if (s_syncCount != last) {
            uint32_t ms = plat_now_ms();
            last = s_syncCount;
            if (ms - lastPrint >= 1000u) {           /* at most one line per second */
                char b1[40], b2[40];
                uint64_t now = ntp_now_ns();
                uint64_t sod = (now / 1000000000ULL) % 86400ULL;   /* UTC seconds-of-day */
                lastPrint = ms;
                SYS_CONSOLE_PRINT("[%02u:%02u:%02u.%03u] offset %-14s delay %-14s\r\n",
                    (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u), (unsigned)(sod % 60u),
                    (unsigned)((now / 1000000ULL) % 1000ULL),
                    fmt_dur(b1, sizeof b1, -s_last_adjust_ns),   /* PC-measured offset = -adjust */
                    fmt_dur(b2, sizeof b2, s_last_delay_ns));
            }
        }
        if (chk_abort(con)) aborted = 1;
        plat_sleep_ms(1);                            /* snappy NTP servicing (was 10) */
    }
    SYS_CONSOLE_PRINT("watch stopped.\r\n");
}

/* "ntp" = status snapshot; "ntp watch [secs]" = continuous per-sync output (UTC). */
static void cmd_ntp(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    (void)pCmdIO;
    if (argc >= 2 && (strcmp(argv[1], "watch") == 0 || strcmp(argv[1], "-w") == 0)) {
        uint32_t secs = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 0u;
        ntp_watch(secs);
    } else {
        ntp_print_status();
    }
}

static const SYS_CMD_DESCRIPTOR ntp_cmd_tbl[] = {
    {"ntp", (SYS_CMD_FNC) cmd_ntp, ": NTP time-counter status; 'ntp watch [secs]' = live per-sync output (q/Ctrl-C)"},
};

void NTP_Init(void)
{
    s_sock = TCPIP_UDP_ServerOpen(IP_ADDRESS_TYPE_IPV4, (UDP_PORT)NTP_PORT, NULL);
    if (s_sock != INVALID_UDP_SOCKET)
        (void)TCPIP_UDP_OptionsSet(s_sock, UDP_OPTION_TX_BUFF, (void *)(uintptr_t)64u);
    SYS_CMD_ADDGRP(ntp_cmd_tbl, sizeof(ntp_cmd_tbl) / sizeof(*ntp_cmd_tbl), "ntp", ": NTP time sync");
}

/* Service the NTP UDP socket. Call once per superloop iteration from APP_Tasks. */
void NTP_Task(void)
{
    uint16_t nb;
    if (s_sock == INVALID_UDP_SOCKET) return;
    while ((nb = TCPIP_UDP_GetIsReady(s_sock)) > 0u) {
        uint8_t in[32], out[32];
        UDP_SOCKET_INFO info;
        uint16_t got = TCPIP_UDP_ArrayGet(s_sock, in, (nb > 32u) ? 32u : nb);
        uint64_t t2 = ntp_now_ns();                 /* receive stamp, as early as possible */
        bool haveInfo = TCPIP_UDP_SocketInfoGet(s_sock, &info);
        (void)TCPIP_UDP_Discard(s_sock);
        if (!haveInfo || got < 1u) continue;

        if (in[0] == OP_REQUEST && got >= 9u) {
            uint64_t t1 = get64(&in[1]);
            uint64_t t3;
            out[0] = OP_REPLY;
            put64(&out[1], t1);                     /* echo t1 */
            put64(&out[9], t2);                     /* t2 = FW receive time */
            t3 = ntp_now_ns();                      /* t3 = FW transmit time */
            put64(&out[17], t3);
            reply_to(&info, out, 25u);
        } else if (in[0] == OP_SET_OFFSET && got >= 9u) {
            int64_t adjust = (int64_t)get64(&in[1]);
            s_offset_ns += adjust;
            s_last_adjust_ns = adjust;
            if (got >= 17u) s_last_delay_ns = (int64_t)get64(&in[9]);  /* PC-measured round-trip */
            s_synced = 1; s_syncCount++;
            out[0] = OP_SET_ACK;
            put64(&out[1], ntp_now_ns());
            reply_to(&info, out, 9u);
        }
    }
}
