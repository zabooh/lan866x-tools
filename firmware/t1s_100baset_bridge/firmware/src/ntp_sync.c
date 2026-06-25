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
 * The follower runs a small PI discipline on the SET_OFFSET stream: it applies the
 * phase correction (P) AND integrates the residual into a frequency term s_rate_ppb
 * (I), which ntp_now_ns() accrues between syncs. This cancels the free-running
 * oscillator drift (~ms/s) instead of leaving a sawtooth - the single biggest
 * accuracy lever. The wire protocol is unchanged (the PC still only sends 'adjust').
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
#define OP_TAP_SET    0x05u    /* PC->FW : [op][enable:1][port:2]  set/clear eth0-timestamp tap   */
#define OP_TAP_REC    0x06u    /* FW->PC : [op][dir][t:8][ipid:2][proto][iplen:2][src:4][dst:4]    */

#define NTP_LOCAL_TZ_OFFSET_S  7200   /* firmware shown in GMT+2 (display only; epoch stays UTC) */

static UDP_SOCKET s_sock = INVALID_UDP_SOCKET;
static int64_t    s_offset_ns = 0;       /* added to the raw counter to align to PC time */
static int64_t    s_last_adjust_ns = 0;  /* last correction the PC applied               */
static int64_t    s_last_delay_ns = 0;   /* last round-trip delay the PC measured         */
static int        s_synced = 0;
static uint32_t   s_syncCount = 0;
static uint64_t   s_lastSyncRaw = 0;     /* raw (monotonic) counter at the last SET_OFFSET */
static int64_t    s_lastInterval = 0;    /* raw interval between the last two syncs         */
static int64_t    s_rate_ppb = 0;        /* learned frequency correction, ns/s (ppb) - PI integral */

#define NTP_KI_DEN  4    /* frequency-loop integral gain Ki = 1/NTP_KI_DEN (Kp = 1, full phase step) */

/* ns of frequency correction accrued since the last sync (integer, overflow-safe:
 * scale the elapsed counter to us first so the product stays well inside int64). */
static int64_t ntp_rate_held(uint64_t raw)
{
    return ((int64_t)((raw - s_lastSyncRaw) / 1000ULL) * s_rate_ppb) / 1000000;
}

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

/* Disciplined NTP time in ns (PC-aligned once a SET_OFFSET has been applied).
 * = raw + phase offset + the frequency correction accrued since the last sync,
 * so the counter keeps tracking PC time *between* syncs instead of drifting. */
uint64_t ntp_now_ns(void)
{
    uint64_t raw = ntp_raw_ns();
    return (uint64_t)((int64_t)raw + s_offset_ns + ntp_rate_held(raw));
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
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

/* --- eth0 timestamp tap: stamp every IPv4 frame crossing eth0 with the synced NTP
 * time and stream the records to a PC collector, so the PC can compute the bridge's
 * one-way delay vs its own NIC capture. The timestamp is taken in the packet hook
 * (no UART/console in the timing path); records are sent later from NTP_Task. */
#define TAP_RING 128u   /* power of two */
typedef struct { uint64_t t; uint32_t src, dst; uint16_t ipid, iplen; uint8_t dir, proto; } tap_rec_t;
static tap_rec_t        s_tring[TAP_RING];
static volatile uint16_t s_thead = 0, s_ttail = 0;
static IPV4_ADDR        s_tap_ip = {0};
static uint16_t         s_tap_port = 0;
static int              s_tap_on = 0;

/* dir 0 = eth0 RX (from bus), 1 = eth0 TX (to bus). Called from the eth0 hooks. */
void ntp_tap_eth0(uint8_t dir, const uint8_t *f, uint16_t len)
{
    uint16_t nh;
    if (!s_tap_on || f == NULL || len < 34u) return;
    if (f[12] != 0x08u || f[13] != 0x00u) return;          /* IPv4 only */
    nh = (uint16_t)((s_thead + 1u) & (TAP_RING - 1u));
    if (nh == s_ttail) return;                             /* ring full: drop */
    s_tring[s_thead].t     = ntp_now_ns();
    s_tring[s_thead].dir   = dir;
    s_tring[s_thead].iplen = (uint16_t)((f[16] << 8) | f[17]);
    s_tring[s_thead].ipid  = (uint16_t)((f[18] << 8) | f[19]);
    s_tring[s_thead].proto = f[23];
    s_tring[s_thead].src   = ((uint32_t)f[26]<<24)|((uint32_t)f[27]<<16)|((uint32_t)f[28]<<8)|f[29];
    s_tring[s_thead].dst   = ((uint32_t)f[30]<<24)|((uint32_t)f[31]<<16)|((uint32_t)f[32]<<8)|f[33];
    s_thead = nh;
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

    /* How long ago the last sync was, and - projected from the last correction over
     * the last sync interval - how far the free-running counter has likely drifted
     * since then (the firmware oscillator is undisciplined, ~ms/s). */
    if (!s_synced) {
        SYS_CONSOLE_PRINT("  last sync  : never (no sync received yet)\r\n");
    } else {
        int64_t elapsed = (int64_t)(up - s_lastSyncRaw);
        char b4[40];
        SYS_CONSOLE_PRINT("  last sync  : %s ago\r\n", fmt_dur(b4, sizeof b4, elapsed));
        if (s_syncCount >= 2u && s_lastInterval > 0) {
            long oscppm = (long)(-(s_rate_ppb) / 1000);      /* oscillator drift the loop has locked to */
            SYS_CONSOLE_PRINT("  osc. drift : %+ld ppm  (frequency-locked, applied)\r\n", oscppm);
            /* The residual phase error caught at the last sync. Once frequency-locked
             * this is mostly measurement jitter, not drift, so it does NOT grow ~linearly
             * with 'elapsed' (holdover is slow) - reporting it directly is the honest figure. */
            SYS_CONSOLE_PRINT("  residual   : ~%s offset at last sync (freq lock keeps holdover slow)\r\n",
                              fmt_dur(b4, sizeof b4, -s_last_adjust_ns));
        } else {
            SYS_CONSOLE_PRINT("  freq lock  : converging (need >= 2 syncs)\r\n");
        }
    }

    SYS_CONSOLE_PRINT("  NTP time   : %llu.%09llu s",
                      (unsigned long long)(now / 1000000000ULL), (unsigned long long)(now % 1000000000ULL));
    if (s_synced) SYS_CONSOLE_PRINT("  (PC-aligned, Unix epoch)");
    SYS_CONSOLE_PRINT("\r\n");
    if (s_synced) {
        uint64_t sod = ((now / 1000000000ULL) + NTP_LOCAL_TZ_OFFSET_S) % 86400ULL;  /* GMT+2 */
        SYS_CONSOLE_PRINT("  local time : %02u:%02u:%02u.%03u (GMT+2)\r\n",
                          (unsigned)(sod / 3600u), (unsigned)((sod % 3600u) / 60u), (unsigned)(sod % 60u),
                          (unsigned)((now / 1000000ULL) % 1000ULL));
    }
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
                uint64_t sod = ((now / 1000000000ULL) + NTP_LOCAL_TZ_OFFSET_S) % 86400ULL;  /* GMT+2 local */
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
            uint64_t raw = ntp_raw_ns();
            /* Consolidate the rate-accrued correction into the phase so the clock is
             * continuous across this update, then re-base the rate accrual at 'raw'. */
            s_offset_ns += ntp_rate_held(raw);
            if (s_synced) s_lastInterval = (int64_t)(raw - s_lastSyncRaw);  /* gap since prior sync */
            s_lastSyncRaw = raw;
            /* PI frequency discipline: 'adjust' is the residual correction the PC wants
             * (= -measured offset). Integrate it (over the interval -> ppb) into the
             * rate term (I, Ki=1/NTP_KI_DEN); apply the phase correction now (P, Kp=1).
             * The huge first-sync adjust is skipped (s_synced still 0) so only the small
             * per-interval residual ever feeds the frequency loop. */
            if (s_synced && s_lastInterval > 0) {
                int64_t iv_us = s_lastInterval / 1000;
                if (iv_us > 0) {
                    int64_t drift_ppb = (adjust * 1000000LL) / iv_us;   /* ns/interval -> ppb */
                    s_rate_ppb += drift_ppb / NTP_KI_DEN;
                }
            }
            s_offset_ns += adjust;
            s_last_adjust_ns = adjust;
            if (got >= 17u) s_last_delay_ns = (int64_t)get64(&in[9]);  /* PC-measured round-trip */
            s_synced = 1; s_syncCount++;
            out[0] = OP_SET_ACK;
            put64(&out[1], ntp_now_ns());
            reply_to(&info, out, 9u);
        } else if (in[0] == OP_TAP_SET && got >= 4u) {
            s_tap_on   = in[1] ? 1 : 0;
            s_tap_port = (uint16_t)((in[2] << 8) | in[3]);
            s_tap_ip.Val = info.sourceIPaddress.v4Add.Val;   /* collector = the PC that asked */
            s_thead = s_ttail = 0;                            /* flush stale records */
            out[0] = OP_SET_ACK; put64(&out[1], ntp_now_ns());
            reply_to(&info, out, 9u);
        }
    }

    /* stream queued eth0 tap records to the PC collector (one UDP datagram each) */
    while (s_tap_on && s_tap_port != 0u && s_ttail != s_thead) {
        const tap_rec_t *r = &s_tring[s_ttail];
        uint8_t rec[23];
        IP_MULTI_ADDRESS dst; dst.v4Add = s_tap_ip;
        if (TCPIP_UDP_TxPutIsReady(s_sock, sizeof rec) < sizeof rec) break;
        rec[0] = OP_TAP_REC; rec[1] = r->dir;
        put64(&rec[2], r->t);
        rec[10] = (uint8_t)(r->ipid >> 8); rec[11] = (uint8_t)r->ipid;
        rec[12] = r->proto;
        rec[13] = (uint8_t)(r->iplen >> 8); rec[14] = (uint8_t)r->iplen;
        put32(&rec[15], r->src); put32(&rec[19], r->dst);
        if (!TCPIP_UDP_DestinationIPAddressSet(s_sock, IP_ADDRESS_TYPE_IPV4, &dst)) break;
        if (!TCPIP_UDP_DestinationPortSet(s_sock, (UDP_PORT)s_tap_port)) break;
        if (TCPIP_UDP_ArrayPut(s_sock, rec, sizeof rec) == sizeof rec) TCPIP_UDP_Flush(s_sock);
        s_ttail = (uint16_t)((s_ttail + 1u) & (TAP_RING - 1u));
    }
}
