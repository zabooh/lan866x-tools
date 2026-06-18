/*
 * diag.c  -  T1S link / endpoint diagnostics for a LAN866x. Pure C, Windows.
 *
 * Reads every diagnostic source the endpoint exposes over RCP and interprets
 * it for one purpose: judging the 10BASE-T1S LINK QUALITY.
 *
 *   - GetStatus (0x1002)        : chip, versions, uptime, reset reason
 *   - GetNetworkStatus (0x1600) : link up/down, arbitration (PLCA), node id, MAC
 *   - ReadDiagnosisData (0x1003): PHY diagnosis channels (SQI / fault / short)
 *   - active probe              : measured RCP loss rate + round-trip time
 *
 * TDR (topology / cable reflectometry) is intentionally NOT used: it needs at
 * least two coordinated nodes and access to the adapter PHY, neither available
 * with a single endpoint on the USB adapter.
 *
 *   lan866x-diag --ip 192.168.0.54 [--probe N] [--raw]
 *
 * Read-only: it only reads diagnostics, never writes flash or pins.
 */

#include <stdio.h>
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static double qpc_ms_per_tick(void)
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    return 1000.0 / (double)f.QuadPart;
}
static long long qpc(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }

static void hexdump(const char *label, const uint8_t *p, uint16_t n)
{
    uint16_t i; int nz = 0;
    printf("    %s (%u B): ", label, n);
    for (i = 0; i < n; ++i) { printf("%02X ", p[i]); if (p[i]) nz = 1; }
    printf("%s\n", nz ? "" : "(all zero)");
}

/* ---- decoders --------------------------------------------------------- */
static void print_reset_reason(uint64_t s)
{
    static const char *bits[] = {
        "Power-On", "Under-voltage VDDC", "Under-voltage VDDA", "BG Error",
        "External", "Watchdog", "Over-temperature", "Device", "Lock-up"
    };
    int i, first = 1;
    printf("  Reset reason   : ");
    if (s == 0) { printf("(none reported)\n"); }
    else {
        for (i = 0; i < 9; ++i)
            if (s & (1ULL << i)) { printf("%s%s", first ? "" : ", ", bits[i]); first = 0; }
        printf("  [Security mode %u]\n", (unsigned)((s >> 9) & 0x3));
    }
}

static void print_mac(const char *label, uint64_t mac)
{
    printf("  %s: %02X:%02X:%02X:%02X:%02X:%02X\n", label,
           (unsigned)((mac >> 40) & 0xFF), (unsigned)((mac >> 32) & 0xFF),
           (unsigned)((mac >> 24) & 0xFF), (unsigned)((mac >> 16) & 0xFF),
           (unsigned)((mac >> 8) & 0xFF), (unsigned)(mac & 0xFF));
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, probeN = 200, raw = 0;
    GetStatusReply_t st;
    GetNetworkStatusReply_t ns;
    ReadDiagnosisDataReply_t dg;
    ReturnCode_t rc;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-diag - read & interpret LAN866x T1S link diagnostics\n\n"
                   "  lan866x-diag [--ip <addr>|--ep <i>] [--probe N] [--raw]\n\n"
                   "  --probe N : number of round-trip probes for loss/latency (default 200)\n"
                   "  --raw     : also dump raw diagnosis channel bytes\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--probe") && i+1<argc) probeN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--raw"))  raw = 1;
    }
    if (probeN < 1) probeN = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x T1S link diagnostics") < 0) return 2;

    printf("\n================ DEVICE ================\n");
    memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) {
        printf("  Chip           : %s\n", st.ChipIdentifier);
        printf("  Running app    : %s\n", st.ActiveApplication);
        printf("  Main app ver   : %s\n", st.MainApplicationVersion);
        printf("  Uptime         : %.1f s\n", (double)(st.UpTime / 1000000ULL) / 1000.0);
        print_reset_reason(st.StartupInformation);
    } else printf("  GetStatus failed\n");

    printf("\n================ T1S NETWORK ================\n");
    memset(&ns, 0, sizeof(ns));
    rc = rcp_get_network_status(&ns);
    if (rc == RT_OK) {
        const char *link = ns.EndpointStatus == 1 ? "UP" : ns.EndpointStatus == 2 ? "DOWN" : "?";
        const char *arb = ns.ArbitrationMode == 0 ? "CSMA/CD (no PLCA!)" :
                          ns.ArbitrationMode == 1 ? "PLCA" :
                          ns.ArbitrationMode == 2 ? "PLCA (no fallback)" : "?";
        printf("  Endpoint link  : %s\n", link);
        printf("  Arbitration    : %s\n", arb);
        printf("  PLCA node id   : %u\n", ns.PLCANodeId);
        print_mac("Endpoint MAC ", ns.EndpointAddress);
        printf("  Endpoint IPv4  : %u.%u.%u.%u\n",
               (unsigned)((ns.EndpointIpV4Address >> 24) & 0xFF), (unsigned)((ns.EndpointIpV4Address >> 16) & 0xFF),
               (unsigned)((ns.EndpointIpV4Address >> 8) & 0xFF), (unsigned)(ns.EndpointIpV4Address & 0xFF));
        printf("  OA-SPI bridge  : %s\n", ns.OaspiStatus == 0 ? "disabled" :
               ns.OaspiStatus == 1 ? "link up" : ns.OaspiStatus == 2 ? "link down" : "?");
    } else printf("  GetNetworkStatus failed (rc=%d)\n", rc);

    printf("\n================ PHY DIAGNOSIS (ReadDiagnosisData) ================\n");
    memset(&dg, 0, sizeof(dg));
    rc = rcp_read_diagnosis_data(&dg);
    if (rc == RT_OK) {
        hexdump("Channel0", dg.Channel0, dg.Channel0Length);
        hexdump("Channel1", dg.Channel1, dg.Channel1Length);
        hexdump("Channel2", dg.Channel2, dg.Channel2Length);
        hexdump("Channel3", dg.Channel3, dg.Channel3Length);
        printf("  (raw PHY diagnosis registers - SQI / fault / short detection)\n");
    } else if (rc == RT_UNKNOWN_METHOD) {
        printf("  Not implemented in this firmware/config build.\n");
        printf("  SQI and short-circuit diagnosis require a configuration with the\n");
        printf("  diagnosis feature enabled (this lighting build does not expose it).\n");
        printf("  -> Link quality below is derived from the measured probe instead.\n");
    } else printf("  ReadDiagnosisData failed (rc=%d)\n", rc);

    /* ---- active link probe: the most direct link-quality measure -------- */
    printf("\n================ LINK PROBE (%d round-trips) ================\n", probeN);
    {
        double per = qpc_ms_per_tick(), sum = 0, mn = 1e9, mx = 0;
        int ok = 0, lost = 0, k;
        rcp_set_retries(0);
        rcp_set_timeout_ms(120);   /* RTT is ~15 ms; a no-reply is a real loss */
        for (k = 0; k < probeN; ++k) {
            long long a = qpc();
            GetStatusReply_t s2; memset(&s2, 0, sizeof(s2));
            if (rcp_get_status(&s2) == RT_OK) {
                double ms = (double)(qpc() - a) * per;
                ok++; sum += ms; if (ms < mn) mn = ms; if (ms > mx) mx = ms;
            } else lost++;
            if ((k % 20) == 19) { printf("\r  probing %d/%d ...", k + 1, probeN); fflush(stdout); }
        }
        rcp_set_retries(3); rcp_set_timeout_ms(1500);
        printf("\r  sent=%d  ok=%d  lost=%d  loss=%.1f%%            \n",
               probeN, ok, lost, 100.0 * lost / probeN);
        if (ok) printf("  round-trip ms  : min %.1f  avg %.1f  max %.1f\n", mn, sum / ok, mx);

        printf("\n================ VERDICT ================\n");
        {
            double loss = 100.0 * lost / probeN, avg = ok ? sum / ok : 1e9;
            const char *q;
            if (ns.EndpointStatus == 2) q = "LINK DOWN";
            else if (loss < 1.0 && avg < 70.0) q = "EXCELLENT";
            else if (loss < 5.0 && avg < 120.0) q = "GOOD";
            else if (loss < 25.0) q = "MARGINAL";
            else q = "POOR";
            printf("  Link quality   : %s   (loss %.1f%%, avg RTT %.0f ms)\n", q, loss, avg);
            if (ns.ArbitrationMode == 0) printf("  ! Not using PLCA - collisions likely on a multidrop bus.\n");
            if (loss >= 5.0) printf("  ! Packet loss elevated - check stubs/termination/cabling/connector.\n");
        }
    }
    return 0;
}
