/*
 * ntpsync.c  -  PC side of the software NTP time sync with the bridge firmware.
 *
 * Opens the firmware's UDP NTP service (default the bridge at 192.168.0.181:30491),
 * runs an NTP t1/t2/t3/t4 exchange to measure the clock offset and round-trip delay
 * between the PC wall clock and the firmware's free-running NTP counter, then pushes
 * a SET_OFFSET (carrying the measured delay) so the firmware counter reads PC time.
 * Query the disciplined counter on the board with the "ntp" CLI command.
 *
 * By default it re-syncs continuously every 250 ms until Ctrl+C, so the firmware
 * counter stays aligned despite drift. Pass --once for a single sync.
 *
 * PC clock: GetSystemTimePreciseAsFileTime() (QPC-disciplined wall clock, ~sub-us),
 * expressed as nanoseconds since the Unix epoch.
 *
 * Usage:
 *   lan866x-ntpsync                         continuous sync every 250 ms (Ctrl+C to stop)
 *   lan866x-ntpsync --once                  sync once and exit
 *   lan866x-ntpsync --ip 192.168.0.180      sync via eth0
 *   lan866x-ntpsync --interval 500 --rounds 6
 *   lan866x-ntpsync --once --no-set         measure offset/delay only, do not discipline
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define NTP_PORT      30491
#define OP_REQUEST    0x01u
#define OP_REPLY      0x02u
#define OP_SET_OFFSET 0x03u
#define OP_SET_ACK    0x04u

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static void put64(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i)); }
static uint64_t get64(const uint8_t *p) { uint64_t v = 0; int i; for (i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }

/* PC wall clock in nanoseconds since the Unix epoch (high resolution). */
static int64_t pc_now_ns(void)
{
    FILETIME ft; ULARGE_INTEGER u;
    GetSystemTimePreciseAsFileTime(&ft);          /* 100 ns ticks since 1601-01-01 */
    u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (int64_t)((u.QuadPart - 116444736000000000ULL) * 100ULL);   /* -> Unix ns */
}

/* Human-readable ns duration (ASCII, signed): ns / us / ms / s. */
static const char *human(int64_t ns, char *b, int n)
{
    const char *sg = (ns < 0) ? "-" : "";
    uint64_t a = (ns < 0) ? (uint64_t)(-ns) : (uint64_t)ns;
    if (a < 1000ULL)            snprintf(b, n, "%s%llu ns", sg, (unsigned long long)a);
    else if (a < 1000000ULL)    snprintf(b, n, "%s%llu.%03llu us", sg, (unsigned long long)(a/1000ULL), (unsigned long long)(a%1000ULL));
    else if (a < 1000000000ULL) snprintf(b, n, "%s%llu.%03llu ms", sg, (unsigned long long)(a/1000000ULL), (unsigned long long)((a%1000000ULL)/1000ULL));
    else                        snprintf(b, n, "%s%llu.%03llu s",  sg, (unsigned long long)(a/1000000000ULL), (unsigned long long)((a%1000000000ULL)/1000000ULL));
    return b;
}

/* One t1/t2/t3/t4 round. Returns 0 on success with *offset (FW-PC) and *delay (ns). */
static int round_once(SOCKET s, int64_t *offset, int64_t *delay)
{
    uint8_t req[9], rep[32];
    int64_t t1, t2, t3, t4; int n;
    t1 = pc_now_ns();
    req[0] = OP_REQUEST; put64(&req[1], (uint64_t)t1);
    if (send(s, (char *)req, sizeof(req), 0) != (int)sizeof(req)) return -1;
    n = recv(s, (char *)rep, sizeof(rep), 0);
    t4 = pc_now_ns();
    if (n < 25 || rep[0] != OP_REPLY) return -1;
    t2 = (int64_t)get64(&rep[9]);
    t3 = (int64_t)get64(&rep[17]);
    *delay  = (t4 - t1) - (t3 - t2);
    *offset = ((t2 - t1) + (t3 - t4)) / 2;
    return 0;
}

/* Measure (best of `rounds` by min delay) and optionally discipline the firmware.
 * Returns 0 on success, filling *bestOffset/*bestDelay (the pre-correction values). */
static int sync_once(SOCKET s, int rounds, int doSet, int64_t *bestOffset, int64_t *bestDelay)
{
    int i, ok = 0; int64_t bo = 0, bd = INT64_MAX;
    for (i = 0; i < rounds; ++i) {
        int64_t off, del;
        if (round_once(s, &off, &del) == 0) {
            ok++;
            if (del < bd) { bd = del; bo = off; }
        }
        if (rounds > 1) Sleep(5);
    }
    if (!ok) return -1;
    *bestOffset = bo; *bestDelay = bd;
    if (doSet) {
        uint8_t set[17], ack[32];
        set[0] = OP_SET_OFFSET;
        put64(&set[1], (uint64_t)(int64_t)(-bo));   /* make FW now == PC now */
        put64(&set[9], (uint64_t)bd);               /* report the measured delay */
        if (send(s, (char *)set, sizeof(set), 0) == (int)sizeof(set))
            (void)recv(s, (char *)ack, sizeof(ack), 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *ip = "192.168.0.181";
    int port = NTP_PORT, rounds = 0, once = 0, doSet = 1, interval = 250, i;
    SOCKET s; struct sockaddr_in sa; WSADATA w; DWORD tmo = 500;
    char hb1[40], hb2[40];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ntpsync - software NTP time sync with the bridge firmware\n"
                   "  (default: re-sync every 250 ms until Ctrl+C)\n"
                   "  --once          sync once and exit\n"
                   "  --interval <ms> continuous re-sync period (default 250)\n"
                   "  --ip <addr>     firmware NTP service IP (default 192.168.0.181)\n"
                   "  --port <n>      UDP port (default 30491)\n"
                   "  --rounds <n>    rounds per sync, best (min-delay) used (default 8 once / 4 cont.)\n"
                   "  --no-set        measure only; do not discipline the firmware counter\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")       && i + 1 < argc) ip = argv[++i];
        else if (!strcmp(argv[i], "--port")       && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rounds")     && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interval")   && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--once"))                       once = 1;
        else if (!strcmp(argv[i], "--no-set"))                     doSet = 0;
    }
    if (rounds < 1) rounds = once ? 8 : 4;
    if (interval < 20) interval = 20;

    if (WSAStartup(MAKEWORD(2, 2), &w)) { printf("WSAStartup failed\n"); return 1; }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { printf("socket failed\n"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tmo, sizeof(tmo));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons((u_short)port); sa.sin_addr.s_addr = inet_addr(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) { printf("connect failed\n"); return 1; }

    if (once) {
        int64_t off, del; int k, m = 0; int64_t res = 0;
        printf("Syncing firmware NTP counter at %s:%d (%d rounds)\n", ip, port, rounds);
        if (sync_once(s, rounds, doSet, &off, &del) != 0) {
            printf("No reply from the firmware NTP service. Reachable? Service running?\n"); return 2;
        }
        printf("  round-trip delay : %s\n", human(del, hb1, sizeof hb1));
        printf("  clock offset     : %s  (FW - PC)\n", human(off, hb2, sizeof hb2));
        if (!doSet) { printf("(--no-set: firmware counter left unchanged)\n"); return 0; }
        for (k = 0; k < 5; ++k) { int64_t o, d; if (round_once(s, &o, &d) == 0) { res += o; m++; } Sleep(20); }
        if (m) printf("  residual offset  : %s  (after sync, bounded by ~delay/2 jitter)\n",
                      human(res / m, hb1, sizeof hb1));
        printf("Done. Query the board with the 'ntp' CLI command.\n");
        return 0;
    }

    /* continuous mode */
    signal(SIGINT, on_sigint);
    printf("Continuous NTP sync to %s:%d every %d ms (%d rounds each). Ctrl+C to stop.\n",
           ip, port, interval, rounds);
    while (g_run) {
        int64_t off, del; SYSTEMTIME lt;
        if (sync_once(s, rounds, doSet, &off, &del) == 0) {
            GetLocalTime(&lt);
            printf("\r[%02d:%02d:%02d.%03d] offset %-14s  delay %-14s   ",
                   lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds,
                   human(off, hb1, sizeof hb1), human(del, hb2, sizeof hb2));
            fflush(stdout);
        } else {
            printf("\r(no reply)                                                  ");
            fflush(stdout);
        }
        Sleep(interval);
    }
    printf("\nStopped.\n");
    closesocket(s); WSACleanup();
    return 0;
}
