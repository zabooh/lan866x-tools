/*
 * ntpsync.c  -  PC side of the software NTP time sync with the bridge firmware.
 *
 * Opens the firmware's UDP NTP service (default the bridge at 192.168.0.181:30491),
 * runs an NTP t1/t2/t3/t4 exchange to measure the clock offset between the PC wall
 * clock and the firmware's free-running NTP counter, then pushes a SET_OFFSET so the
 * firmware counter reads PC time. After that the firmware keeps its own disciplined
 * counter (query it on the board with the "ntp" CLI command) - so firmware events can
 * be timestamped on the PC timebase.
 *
 * PC clock: GetSystemTimePreciseAsFileTime() (QPC-disciplined wall clock, ~sub-us),
 * expressed as nanoseconds since the Unix epoch to match typical capture timestamps.
 *
 * Usage:
 *   lan866x-ntpsync                         sync the bridge at 192.168.0.181
 *   lan866x-ntpsync --ip 192.168.0.180      (eth0 side)
 *   lan866x-ntpsync --rounds 20 --no-set    measure only, do not discipline
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define NTP_PORT      30491
#define OP_REQUEST    0x01u
#define OP_REPLY      0x02u
#define OP_SET_OFFSET 0x03u
#define OP_SET_ACK    0x04u

static void put64(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i)); }
static uint64_t get64(const uint8_t *p) { uint64_t v = 0; int i; for (i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }

/* PC wall clock in nanoseconds since the Unix epoch (high resolution). */
static int64_t pc_now_ns(void)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimePreciseAsFileTime(&ft);          /* 100 ns ticks since 1601-01-01 */
    u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    /* 1601->1970 = 11644473600 s = 116444736000000000 in 100 ns units */
    return (int64_t)((u.QuadPart - 116444736000000000ULL) * 100ULL);
}

int main(int argc, char **argv)
{
    const char *ip = "192.168.0.181";
    int rounds = 10, doSet = 1, i;
    SOCKET s;
    struct sockaddr_in sa;
    WSADATA w;
    DWORD tmo = 500;
    int64_t bestDelay = INT64_MAX, bestOffset = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ntpsync - software NTP time sync with the bridge firmware\n"
                   "  --ip <addr>     firmware NTP service IP (default 192.168.0.181)\n"
                   "  --port <n>      UDP port (default 30491)\n"
                   "  --rounds <n>    measurement rounds, best (min-delay) is used (default 10)\n"
                   "  --no-set        measure only; do not discipline the firmware counter\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")     && i + 1 < argc) ip = argv[++i];
        else if (!strcmp(argv[i], "--port")     && i + 1 < argc) ; /* handled below */
        else if (!strcmp(argv[i], "--rounds")   && i + 1 < argc) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-set"))                   doSet = 0;
    }
    int port = NTP_PORT;
    for (i = 1; i < argc; ++i) if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[i + 1]);
    if (rounds < 1) rounds = 1;

    if (WSAStartup(MAKEWORD(2, 2), &w)) { printf("WSAStartup failed\n"); return 1; }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { printf("socket failed\n"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&tmo, sizeof(tmo));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons((u_short)port);
    sa.sin_addr.s_addr = inet_addr(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) { printf("connect failed\n"); return 1; }

    printf("Syncing firmware NTP counter at %s:%d  (%d rounds)\n", ip, port, rounds);
    int ok = 0;
    for (i = 0; i < rounds; ++i) {
        uint8_t req[9], rep[32];
        int64_t t1, t2, t3, t4, delay, offset;
        int n;
        t1 = pc_now_ns();
        req[0] = OP_REQUEST; put64(&req[1], (uint64_t)t1);
        if (send(s, (char *)req, sizeof(req), 0) != (int)sizeof(req)) continue;
        n = recv(s, (char *)rep, sizeof(rep), 0);
        t4 = pc_now_ns();
        if (n < 25 || rep[0] != OP_REPLY) continue;
        t2 = (int64_t)get64(&rep[9]);
        t3 = (int64_t)get64(&rep[17]);
        delay  = (t4 - t1) - (t3 - t2);                 /* round-trip minus FW processing */
        offset = ((t2 - t1) + (t3 - t4)) / 2;           /* FW clock - PC clock */
        ok++;
        if (delay < bestDelay) { bestDelay = delay; bestOffset = offset; }
        Sleep(20);
    }
    if (!ok) { printf("No reply from the firmware NTP service. Reachable? Service running?\n"); return 2; }

    printf("  best round-trip delay : %lld ns (%.3f ms)\n", (long long)bestDelay, bestDelay / 1e6);
    printf("  measured clock offset : %lld ns (FW - PC)\n", (long long)bestOffset);

    if (!doSet) { printf("(--no-set: firmware counter left unchanged)\n"); return 0; }

    /* discipline the firmware: make FW now == PC now  ->  adjust = -offset */
    {
        uint8_t set[9], ack[32]; int n;
        put64(&set[1], (uint64_t)(int64_t)(-bestOffset)); set[0] = OP_SET_OFFSET;
        if (send(s, (char *)set, sizeof(set), 0) != (int)sizeof(set)) { printf("SET_OFFSET send failed\n"); return 3; }
        n = recv(s, (char *)ack, sizeof(ack), 0);
        if (n >= 9 && ack[0] == OP_SET_ACK) {
            int64_t fwnow = (int64_t)get64(&ack[1]);
            printf("  SET_OFFSET applied. FW NTP now = %lld.%09lld s (Unix epoch)\n",
                   (long long)(fwnow / 1000000000LL), (long long)(fwnow % 1000000000LL));
        } else { printf("no SET_ACK\n"); return 3; }
    }

    /* verify: one more exchange should show a near-zero residual offset */
    {
        uint8_t req[9], rep[32]; int64_t t1, t2, t3, t4, off = 0; int n, m = 0, k;
        for (k = 0; k < 5; ++k) {
            t1 = pc_now_ns(); req[0] = OP_REQUEST; put64(&req[1], (uint64_t)t1);
            if (send(s, (char *)req, sizeof(req), 0) != (int)sizeof(req)) continue;
            n = recv(s, (char *)rep, sizeof(rep), 0); t4 = pc_now_ns();
            if (n < 25 || rep[0] != OP_REPLY) continue;
            t2 = (int64_t)get64(&rep[9]); t3 = (int64_t)get64(&rep[17]);
            off += ((t2 - t1) + (t3 - t4)) / 2; m++; Sleep(20);
        }
        if (m) printf("  residual offset after sync: ~%lld ns (avg of %d) - bounded by ~delay/2 jitter\n",
                      (long long)(off / m), m);
    }

    printf("Done. Query the board with the 'ntp' CLI command to see the disciplined counter.\n");
    closesocket(s); WSACleanup();
    return 0;
}
