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
#include <stdlib.h>

#include "definitions.h"
#include "system/command/sys_command.h"
#include "config/default/system/console/sys_console.h"
#include "lan866x_cli.h"
#include "rcp.h"
#include "plat.h"     /* plat_sleep_ms(): pumps the stack while pacing control traffic */

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

/* Select the first discovered endpoint as the RCP target; false if none yet. */
static bool sel_first_ep(void)
{
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    if (!s_rcp_inited) return false;
    if (rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS) == 0u) return false;
    return rcp_select_endpoint(0u);
}

/* ====================== diag (mirrors lan866x-diag.exe) ==================== */
static void diag_hexdump(const char *label, const uint8_t *p, uint16_t n)
{
    uint16_t i; int nz = 0;
    SYS_CONSOLE_PRINT("    %s (%u B): ", label, (unsigned)n);
    for (i = 0u; i < n; i++) { SYS_CONSOLE_PRINT("%02X ", p[i]); if (p[i]) nz = 1; }
    SYS_CONSOLE_PRINT("%s\r\n", nz ? "" : "(all zero)");
}

static void diag_reset_reason(uint64_t s)
{
    static const char *bits[] = {
        "Power-On", "Under-voltage VDDC", "Under-voltage VDDA", "BG Error",
        "External", "Watchdog", "Over-temperature", "Device", "Lock-up"
    };
    int i, first = 1;
    SYS_CONSOLE_PRINT("  Reset reason : ");
    if (s == 0u) { SYS_CONSOLE_PRINT("(none reported)\r\n"); return; }
    for (i = 0; i < 9; i++)
        if (s & (1ULL << i)) { SYS_CONSOLE_PRINT("%s%s", first ? "" : ", ", bits[i]); first = 0; }
    SYS_CONSOLE_PRINT("  [Security mode %u]\r\n", (unsigned)((s >> 9) & 0x3u));
}

static void cmd_diag(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    GetStatusReply_t st;
    GetNetworkStatusReply_t ns;
    ReadDiagnosisDataReply_t dg;
    ReturnCode_t rc;
    uint32_t probeN = 20u;
    (void)pCmdIO;

    if (argc >= 2) probeN = (uint32_t)strtoul(argv[1], NULL, 10);
    if (probeN < 1u)   probeN = 1u;
    if (probeN > 500u) probeN = 500u;

    if (!sel_first_ep()) {
        SYS_CONSOLE_PRINT("[lan866x] no endpoint yet - run 'discovery' first\r\n");
        return;
    }
    rcp_set_timeout_ms(1000); rcp_set_retries(1);

    SYS_CONSOLE_PRINT("\r\n================ DEVICE ================\r\n");
    memset(&st, 0, sizeof(st));
    if (rcp_get_status(&st) == RT_OK) {
        unsigned long long up = st.UpTime / 1000000000ULL;
        SYS_CONSOLE_PRINT("  Chip         : %s\r\n", st.ChipIdentifier);
        SYS_CONSOLE_PRINT("  Running app  : %s\r\n", st.ActiveApplication);
        SYS_CONSOLE_PRINT("  Main app ver : %s\r\n", st.MainApplicationVersion);
        SYS_CONSOLE_PRINT("  Uptime       : %lluh %llum %llus\r\n", up/3600ULL, (up%3600ULL)/60ULL, up%60ULL);
        diag_reset_reason(st.StartupInformation);
    } else {
        SYS_CONSOLE_PRINT("  GetStatus failed\r\n");
    }

    SYS_CONSOLE_PRINT("\r\n================ T1S NETWORK ================\r\n");
    memset(&ns, 0, sizeof(ns));
    rc = rcp_get_network_status(&ns);
    if (rc == RT_OK) {
        uint64_t mac = ns.EndpointAddress;
        SYS_CONSOLE_PRINT("  Endpoint link: %s\r\n", ns.EndpointStatus==1?"UP":ns.EndpointStatus==2?"DOWN":"?");
        SYS_CONSOLE_PRINT("  Arbitration  : %s\r\n", ns.ArbitrationMode==0?"CSMA/CD (no PLCA!)":
                          ns.ArbitrationMode==1?"PLCA":ns.ArbitrationMode==2?"PLCA (no fallback)":"?");
        SYS_CONSOLE_PRINT("  PLCA node id : %u\r\n", ns.PLCANodeId);
        SYS_CONSOLE_PRINT("  Endpoint MAC : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
            (unsigned)((mac>>40)&0xFF),(unsigned)((mac>>32)&0xFF),(unsigned)((mac>>24)&0xFF),
            (unsigned)((mac>>16)&0xFF),(unsigned)((mac>>8)&0xFF),(unsigned)(mac&0xFF));
        SYS_CONSOLE_PRINT("  Endpoint IPv4: %u.%u.%u.%u\r\n",
            (unsigned)((ns.EndpointIpV4Address>>24)&0xFF),(unsigned)((ns.EndpointIpV4Address>>16)&0xFF),
            (unsigned)((ns.EndpointIpV4Address>>8)&0xFF),(unsigned)(ns.EndpointIpV4Address&0xFF));
        SYS_CONSOLE_PRINT("  OA-SPI bridge: %s\r\n", ns.OaspiStatus==0?"disabled":
                          ns.OaspiStatus==1?"link up":ns.OaspiStatus==2?"link down":"?");
    } else {
        SYS_CONSOLE_PRINT("  GetNetworkStatus failed (rc=%d)\r\n", rc);
    }

    SYS_CONSOLE_PRINT("\r\n================ PHY DIAGNOSIS ================\r\n");
    memset(&dg, 0, sizeof(dg));
    rc = rcp_read_diagnosis_data(&dg);
    if (rc == RT_OK) {
        diag_hexdump("Channel0", dg.Channel0, dg.Channel0Length);
        diag_hexdump("Channel1", dg.Channel1, dg.Channel1Length);
        diag_hexdump("Channel2", dg.Channel2, dg.Channel2Length);
        diag_hexdump("Channel3", dg.Channel3, dg.Channel3Length);
    } else if (rc == RT_UNKNOWN_METHOD) {
        SYS_CONSOLE_PRINT("  Not implemented in this firmware/config build (lighting build).\r\n");
    } else {
        SYS_CONSOLE_PRINT("  ReadDiagnosisData failed (rc=%d)\r\n", rc);
    }

    SYS_CONSOLE_PRINT("\r\n================ LINK PROBE (%u round-trips) ================\r\n", (unsigned)probeN);
    {
        uint32_t freq = SYS_TIME_FrequencyGet();
        uint32_t ok = 0u, lost = 0u, k;
        uint64_t sumus = 0u, mnus = 0xFFFFFFFFFFFFFFFFULL, mxus = 0u;
        rcp_set_retries(0); rcp_set_timeout_ms(400);
        for (k = 0u; k < probeN; k++) {
            GetStatusReply_t s2;
            uint64_t a = SYS_TIME_Counter64Get();
            memset(&s2, 0, sizeof(s2));
            if (rcp_get_status(&s2) == RT_OK) {
                uint64_t us = freq ? (((SYS_TIME_Counter64Get() - a) * 1000000ULL) / freq) : 0u;
                ok++; sumus += us; if (us < mnus) mnus = us; if (us > mxus) mxus = us;
            } else {
                lost++;
            }
            SYS_CONSOLE_PRINT("\r  probing %u/%u ...", (unsigned)(k + 1u), (unsigned)probeN);
            plat_sleep_ms(10);   /* small pace; pumps stack + console (live progress) */
        }
        SYS_CONSOLE_PRINT("\r  probes=%u completed=%u lost=%u loss=%u%%          \r\n",
            (unsigned)probeN, (unsigned)ok, (unsigned)lost, (unsigned)(100u*lost/probeN));
        if (ok) {
            uint64_t avg = sumus/ok;
            SYS_CONSOLE_PRINT("  RTT: min %u.%03u  avg %u.%03u  max %u.%03u ms\r\n",
                (unsigned)(mnus/1000u),(unsigned)(mnus%1000u),
                (unsigned)(avg/1000u), (unsigned)(avg%1000u),
                (unsigned)(mxus/1000u),(unsigned)(mxus%1000u));
        }
        SYS_CONSOLE_PRINT("  Verdict: %s\r\n",
            (ns.EndpointStatus==2)?"LINK DOWN":
            (probeN && (100u*lost/probeN) < 3u)?"HEALTHY (T1S wire fine)":"DEGRADED - check link");
    }

    SYS_CONSOLE_PRINT("\r\n================ BANDWIDTH (RCP goodput) ================\r\n");
    {
        extern volatile uint32_t g_plat_tx_bytes, g_plat_rx_bytes;
        uint32_t freq = SYS_TIME_FrequencyGet();
        uint32_t bn = 30u, okb = 0u, kk;
        uint32_t tx0 = g_plat_tx_bytes, rx0 = g_plat_rx_bytes;
        uint64_t t0  = SYS_TIME_Counter64Get();

        rcp_set_retries(0); rcp_set_timeout_ms(400);
        for (kk = 0u; kk < bn; kk++) {           /* back-to-back, no inter-request gap = max rate */
            GetStatusReply_t s3; memset(&s3, 0, sizeof(s3));
            if (rcp_get_status(&s3) == RT_OK) okb++;
            if ((kk % 10u) == 9u)
                SYS_CONSOLE_PRINT("\r  measuring %u/%u ...", (unsigned)(kk + 1u), (unsigned)bn);
        }
        SYS_CONSOLE_PRINT("\r                          \r");   /* clear progress line */
        {
            uint64_t dt    = SYS_TIME_Counter64Get() - t0;
            uint32_t ms    = freq ? (uint32_t)((dt * 1000ULL) / freq) : 0u;
            uint32_t txb   = g_plat_tx_bytes - tx0;
            uint32_t rxb   = g_plat_rx_bytes - rx0;
            uint32_t bytes = txb + rxb;
            uint32_t kbps  = ms ? ((bytes * 8u) / ms) : 0u;   /* bits/ms = kbit/s */
            uint32_t rtps  = ms ? ((okb * 1000u) / ms) : 0u;
            SYS_CONSOLE_PRINT("  %u round-trips in %u ms (%u ok), %u UDP payload bytes (tx %u + rx %u)\r\n",
                (unsigned)bn, (unsigned)ms, (unsigned)okb, (unsigned)bytes, (unsigned)txb, (unsigned)rxb);
            SYS_CONSOLE_PRINT("  Round-trips/s: %u    RCP goodput: ~%u kbit/s\r\n",
                (unsigned)rtps, (unsigned)kbps);
            SYS_CONSOLE_PRINT("  (sequential request/reply bound by RCP latency, not the\r\n");
            SYS_CONSOLE_PRINT("   10BASE-T1S 10 Mbit/s line rate.)\r\n");
        }
    }

    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* ====================== ledblink (mirrors lan866x-ledblink.exe) ============ */
static bool led_set(uint16_t handle, int value)
{
    SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
    sv.GpioValues[0] = (uint8_t)(handle >> 8);
    sv.GpioValues[1] = (uint8_t)(handle & 0xFF);
    sv.GpioValues[2] = (uint8_t)(value ? 1 : 0);
    sv.GpioValuesLength = 3;
    return rcp_set_gpio(&sv) == RT_OK;
}

static void cmd_ledblink(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    static const uint8_t LEDS[3] = { 2u, 6u, 10u };  /* PA02/PA06/PA10 = LD1/LD2/LD3 */
    uint16_t handle[3];
    uint32_t laps = 3u, beat = 300u, lap;
    int i;
    (void)pCmdIO;

    if (argc >= 2) laps = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) beat = (uint32_t)strtoul(argv[2], NULL, 10);
    if (laps < 1u)    laps = 1u;
    if (laps > 50u)   laps = 50u;     /* bounded: must not freeze the bridge superloop forever */
    if (beat < 20u)   beat = 20u;
    if (beat > 2000u) beat = 2000u;

    if (!sel_first_ep()) {
        SYS_CONSOLE_PRINT("[lan866x] no endpoint yet - run 'discovery' first\r\n");
        return;
    }
    rcp_set_timeout_ms(800); rcp_set_retries(2);

    /* Open each LED pin as a GPIO output (release first, like the host tool). */
    for (i = 0; i < 3; i++) {
        ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
        memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = LEDS[i]; rel.PinIdListLength = 1;
        rcp_release_digital_pins(&rel); plat_sleep_ms(20);
        memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
        ov.PinIdGpio = LEDS[i]; ov.Direction = 1;   /* output, starts LOW */
        if (rcp_open_gpio(&ov, &orep) != RT_OK) {
            SYS_CONSOLE_PRINT("[lan866x] OpenGpio failed on PA%02u - aborting\r\n", (unsigned)LEDS[i]);
            rcp_set_timeout_ms(1500); rcp_set_retries(3);
            return;
        }
        handle[i] = orep.HandleGpio; plat_sleep_ms(20);
    }

    SYS_CONSOLE_PRINT("[lan866x] running light PA02/PA06/PA10: %u laps, %u ms/step\r\n",
                      (unsigned)laps, (unsigned)beat);
    for (lap = 0u; lap < laps; lap++) {
        for (i = 0; i < 3; i++) {
            led_set(handle[i], 1);
            plat_sleep_ms(beat);
            led_set(handle[i], 0);
        }
    }

    /* Clean up: LEDs off + release pins. */
    for (i = 0; i < 3; i++) {
        ReleaseDigitalPinsVar_t rel;
        led_set(handle[i], 0);
        memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = LEDS[i]; rel.PinIdListLength = 1;
        rcp_release_digital_pins(&rel); plat_sleep_ms(20);
    }
    SYS_CONSOLE_PRINT("[lan866x] done, LEDs off.\r\n");
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

/* clickdemo [seconds] [fps] - run the Thumbstick+Proximity -> RGB demo. */
/* ====================== gpiomax (max-speed GPIO toggle benchmark) ========== */
/* Mirrors lan866x-gpiomax.exe: keeps a pipeline of SetGpio requests in flight
 * and measures the achieved toggle rate. Reply tallies are updated by the async
 * callback (runs inline from rcp_async_poll(); single-thread - no rcp_* here). */
static unsigned gm_ok, gm_to, gm_err;
static int      gm_inflight;
static void gm_done(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx; (void)rx; (void)rxLen;
    if (rc == RT_OK)            gm_ok++;
    else if (rc == RT_TIMEOUT)  gm_to++;
    else                        gm_err++;
    if (gm_inflight > 0) gm_inflight--;
}

static void cmd_gpiomax(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint32_t secs = 3u, start, endt, elapsed, drainEnd;
    uint8_t  pin  = 2u;        /* PA02 / LD1 */
    int      depth = 16, value = 0;
    unsigned sent = 0u;
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    uint16_t handle;
    (void)pCmdIO;

    if (argc >= 2) secs  = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) pin   = (uint8_t)strtoul(argv[2], NULL, 10);
    if (argc >= 4) depth = (int)strtoul(argv[3], NULL, 10);
    if (secs < 1u)  secs = 1u;
    if (secs > 10u) secs = 10u;        /* bounded: must not block the bridge too long */
    if (pin > 15u)  pin  = 2u;
    if (depth < 1)  depth = 1;
    if (depth > 16) depth = 16;        /* RCP_ASYNC_MAX */

    if (!sel_first_ep()) {
        SYS_CONSOLE_PRINT("[lan866x] no endpoint yet - run 'discovery' first\r\n");
        return;
    }

    /* Open the pin as a GPIO output (release first, like the host tool). */
    rcp_set_timeout_ms(800); rcp_set_retries(2);
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel); plat_sleep_ms(20);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = pin; ov.Direction = 1;            /* output, starts LOW */
    if (rcp_open_gpio(&ov, &orep) != RT_OK) {
        SYS_CONSOLE_PRINT("[lan866x] OpenGpio failed on PA%02u - aborting\r\n", (unsigned)pin);
        rcp_set_timeout_ms(1500); rcp_set_retries(3);
        return;
    }
    handle = orep.HandleGpio; plat_sleep_ms(20);

    gm_ok = gm_to = gm_err = 0u; gm_inflight = 0;
    rcp_set_async_timeout_ms(100);   /* free a dropped reply's slot quickly */

    SYS_CONSOLE_PRINT("[lan866x] max-speed toggle of PA%02u for %u s (pipeline depth %d)...\r\n",
                      (unsigned)pin, (unsigned)secs, depth);

    start = plat_now_ms();
    endt  = start + secs * 1000u;
    while ((int32_t)(plat_now_ms() - endt) < 0) {
        while (gm_inflight < depth) {
            uint8_t p[16];
            uint16_t n = rcp_enc_gpio_set(p, sizeof(p), handle, (uint8_t)(value ^ 1));
            if (!n || rcp_async_request(0x1330u, p, n, gm_done, NULL) != RT_OK)
                break;                               /* slot/buffer busy: drain first */
            value ^= 1; sent++; gm_inflight++;
        }
        rcp_async_poll();
        plat_sleep_ms(1);   /* pump TCPIP stack + console: UDP flows, bridge keeps running */
    }
    /* Drain the last in-flight replies (bounded grace). */
    drainEnd = plat_now_ms() + 300u;
    while (gm_inflight > 0 && (int32_t)(plat_now_ms() - drainEnd) < 0) {
        rcp_async_poll(); plat_sleep_ms(1);
    }

    elapsed = plat_now_ms() - start;
    if (elapsed == 0u) elapsed = secs * 1000u;
    SYS_CONSOLE_PRINT("\r\n============= GPIO TOGGLE BENCHMARK =============\r\n");
    SYS_CONSOLE_PRINT("  Pin PA%02u   window %u ms   depth %d\r\n",
                      (unsigned)pin, (unsigned)elapsed, depth);
    SYS_CONSOLE_PRINT("  Commanded: %u toggles -> %u toggles/s\r\n",
                      sent, (unsigned)((uint64_t)sent * 1000u / elapsed));
    SYS_CONSOLE_PRINT("  Confirmed: %u toggles -> %u toggles/s (~%u Hz square wave)\r\n",
                      gm_ok, (unsigned)((uint64_t)gm_ok * 1000u / elapsed),
                      (unsigned)((uint64_t)gm_ok * 1000u / elapsed / 2u));
    SYS_CONSOLE_PRINT("  Reply timeouts: %u  errors: %u", gm_to, gm_err);
    if (sent) SYS_CONSOLE_PRINT("  (loss %u%%)", (unsigned)((gm_to + gm_err) * 100u / sent));
    SYS_CONSOLE_PRINT("\r\n================================================\r\n");

    /* Clean up: pin LOW + release, restore defaults. */
    led_set(handle, 0);
    memset(&rel, 0, sizeof(rel)); rel.PinIdList[0] = pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel); plat_sleep_ms(20);
    rcp_set_async_timeout_ms(150);
    rcp_set_timeout_ms(1500); rcp_set_retries(3);
}

static void cmd_clickdemo(SYS_CMD_DEVICE_NODE *pCmdIO, int argc, char **argv)
{
    uint32_t seconds = 20u;
    int fps = 50;
    (void)pCmdIO;
    if (argc >= 2) seconds = (uint32_t)strtoul(argv[1], NULL, 10);
    if (argc >= 3) fps     = (int)strtoul(argv[2], NULL, 10);
    if (seconds < 1u)  seconds = 1u;
    if (seconds > 600u) seconds = 600u;   /* bounded: must not block the bridge forever */
    clickdemo_run(seconds, fps, 128, 400, 64);
}

static const SYS_CMD_DESCRIPTOR lan866x_cmd_tbl[] = {
    {"discovery", (SYS_CMD_FNC) cmd_discovery, ": list LAN866x endpoints + full status (like lan866x-discovery.exe)"},
    {"diag",      (SYS_CMD_FNC) cmd_diag,      ": T1S link diagnostics (diag [probeCount])"},
    {"ledblink",  (SYS_CMD_FNC) cmd_ledblink,  ": LED running light PA02/06/10 (ledblink [laps] [ms])"},
    {"gpiomax",   (SYS_CMD_FNC) cmd_gpiomax,   ": max-speed GPIO toggle benchmark (gpiomax [secs] [pin] [depth])"},
    {"clickdemo", (SYS_CMD_FNC) cmd_clickdemo, ": Thumbstick+Proximity -> RGB displays (clickdemo [s] [fps])"},
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
