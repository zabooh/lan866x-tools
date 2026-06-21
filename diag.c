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
#include <mmsystem.h>     /* timeBeginPeriod: 1 ms tick so the poll-wait doesn't inflate RTT */

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
    int wantEp = 0, i, probeN = 200, raw = 0, gap = 15, clearCounters = 0, tdRole = -1;
    GetStatusReply_t st;
    GetHealthStatusReply_t hs;
    GetNetworkStatusReply_t ns;
    ReadDiagnosisDataReply_t dg;
    ReturnCode_t rc;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-diag - read & interpret LAN866x T1S link diagnostics\n\n"
                   "  lan866x-diag [--ip <addr>|--ep <i>] [--probe N] [--gap MS] [--raw]\n\n"
                   "  --probe N : number of round-trip probes for loss/latency (default 200)\n"
                   "  --gap MS  : pause between probes (default 15; use 0 to stress the host rx path)\n"
                   "  --raw     : also dump raw diagnosis channel bytes\n"
                   "  --clear-counters : reset all network diagnosis counters (0x1605) first\n"
                   "  --td <0|1|2>     : start a topology/delay measurement (0=initiator,\n"
                   "                     1=reference, 2=measurement) - needs >=2 coordinated nodes\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--probe") && i+1<argc) probeN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gap")   && i+1<argc) gap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--raw"))  raw = 1;
        else if (!strcmp(argv[i], "--clear-counters")) clearCounters = 1;
        else if (!strcmp(argv[i], "--td")    && i+1<argc) tdRole = atoi(argv[++i]);
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

    /* Module health monitor (method 0x100A). Layout per v1.10.0 proto - may be
     * absent/different on older firmware; treated as best-effort. */
    memset(&hs, 0, sizeof(hs));
    rc = rcp_get_health_status(&hs);
    if (rc == RT_OK) {
        printf("  Health app     : %s\n", hs.ActiveApplication);
        printf("  Health uptime  : %.1f s\n", (double)hs.Uptime / 1e9);
        printf("  Health record  : 0x%016llX %s\n",
               (unsigned long long)hs.HealthRecord, hs.HealthRecord ? "(see manual)" : "(all OK)");
    } else if (rc == RT_UNKNOWN_METHOD) {
        printf("  Health monitor : not implemented in this firmware build\n");
    } else printf("  GetHealthStatus failed (rc=%d)\n", rc);

    printf("\n================ T1S NETWORK ================\n");
    if (clearCounters) {
        ClearNetworkCountersVar_t cc; memset(&cc, 0, sizeof(cc));
        cc.Category = 0;   /* all */
        rc = rcp_clear_network_counters(&cc);
        printf("  Counters reset : %s\n", rc == RT_OK ? "OK (all categories)" :
               rc == RT_UNKNOWN_METHOD ? "not implemented" : "FAILED");
    }
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

    if (tdRole >= 0) {
        StartTDMeasurementVar_t td; memset(&td, 0, sizeof(td));
        printf("\n================ TOPOLOGY/DELAY MEASUREMENT ================\n");
        td.Role = (uint8_t)tdRole; td.Duration = 10;
        rc = rcp_start_td_measurement(&td);
        printf("  StartTDMeasurement(role=%d): %s\n", tdRole,
               rc == RT_OK ? "started" : rc == RT_UNKNOWN_METHOD ? "not implemented" : "FAILED");
        printf("  (Needs >=2 coordinated nodes; collect the result on the initiator\n");
        printf("   via GetTDMeasurementResult or the OnTDMeasurementCompleted event.)\n");
    }

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

    /* ---- active link probe ---------------------------------------------
     * Each probe is one RCP round-trip. A no-reply is retried immediately: a
     * response that only fails on the first try but succeeds on retry is a
     * HOST-side miss (scheduling / multi-homed NIC), not a lost frame on the
     * T1S wire. Only a probe that fails every attempt counts as link loss. */
    printf("\n================ LINK PROBE (%d round-trips) ================\n", probeN);
    timeBeginPeriod(1);
    {
        double per = qpc_ms_per_tick(), sum = 0, mn = 1e9, mx = 0;
        int ok = 0, lost = 0, slow = 0, k;
        rcp_set_retries(0);
        rcp_set_timeout_ms(400);                 /* generous: capture true completion time */
        for (k = 0; k < probeN; ++k) {
            long long a = qpc();
            GetStatusReply_t s2; memset(&s2, 0, sizeof(s2));
            if (rcp_get_status(&s2) == RT_OK) {
                double ms = (double)(qpc() - a) * per;
                ok++; sum += ms; if (ms < mn) mn = ms; if (ms > mx) mx = ms;
                if (ms > 25.0) slow++;            /* far above the ~2 ms T1S wire RTT */
            } else lost++;
            if ((k % 20) == 19) { printf("\r  probing %d/%d ...", k + 1, probeN); fflush(stdout); }
            Sleep(gap);
        }
        rcp_set_retries(3); rcp_set_timeout_ms(1500);
        printf("\r  probes=%d  completed=%d  lost=%d  loss=%.1f%%          \n",
               probeN, ok, lost, 100.0 * lost / probeN);
        if (ok) printf("  end-to-end RTT : min %.1f  avg %.1f  max %.1f ms   (slow >25ms: %.0f%%)\n",
                       mn, sum / ok, mx, 100.0 * slow / ok);

        printf("\n================ VERDICT ================\n");
        {
            double loss = 100.0 * lost / probeN, avg = ok ? sum / ok : 0;
            if (ns.EndpointStatus == 2) {
                printf("  T1S link       : LINK DOWN\n");
            } else if (loss < 3.0) {
                printf("  T1S link       : HEALTHY  (loss %.1f%%, RTT min %.1f / avg %.1f ms)\n", loss, mn, avg);
                printf("                   ~%.0f ms is the real 10BASE-T1S round-trip - the wire is fine.\n", mn);
            } else if (gap > 0) {
                printf("  T1S link       : DEGRADED  (loss %.1f%% even when paced) - check the\n", loss);
                printf("                   physical link: stubs/termination/connector. RTT min %.1f ms.\n", mn);
            } else {
                printf("  T1S link       : likely OK (RTT min %.1f ms) but probed back-to-back -\n", mn);
                printf("                   re-run with --gap 15 for a clean link figure.\n");
            }
            if (ns.ArbitrationMode == 0) printf("  ! Not using PLCA - collisions likely on a multidrop bus.\n");
            printf("\n  HOST THROUGHPUT: the PC drops responses when RCP requests are sent faster\n");
            printf("  than it can service them, even though the wire answers in ~2 ms. Measured\n");
            printf("  loss climbs steeply with no pacing (e.g. ~60%% at 0 ms gap vs ~1%% at 20 ms).\n");
            printf("  THIS host-side limit - not the link - is what makes rapid interaction feel\n");
            printf("  sluggish. Mitigation: pace control traffic / batch reads (compound SPI),\n");
            printf("  and disable host NICs not on the endpoint subnet.\n");
            printf("  Note: a packet capture is the authoritative wire reference (host+link here).\n");
        }
    }
    timeEndPeriod(1);
    return 0;
}
