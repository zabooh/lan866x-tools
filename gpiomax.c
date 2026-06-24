/*
 * gpiomax.c  -  Maximum-speed GPIO toggle benchmark over SOME/IP (RCP). Pure C.
 *
 * Drives ONE on-board LED GPIO (default PA02 / LD1) as fast as possible and
 * measures the achieved toggle rate. Unlike `lan866x-ledtoggle` (one async
 * SetGpio per beat) and the blocking `rcp_set_gpio()` (which paces itself with a
 * fixed 2 ms poll sleep, capping it at ~500/s regardless of the link), this tool
 * keeps a PIPELINE of up to RCP_ASYNC_MAX SetGpio requests in flight and polls
 * for replies, so it reaches the true sustained rate.
 *
 * It reports two numbers:
 *   - COMMANDED  = SetGpio requests issued per second (the rate the GPIO is told
 *                  to toggle; the requests reach the endpoint).
 *   - CONFIRMED  = replies received per second (round-trips the host could verify).
 * On a clean link the two match. On the Windows host they can diverge: under
 * back-to-back requests the host drops a fraction of the *replies* (the toggle
 * still happened) - so CONFIRMED is a lower bound. Run the firmware `gpiomax`
 * command for a host-drop-free figure.
 *
 * Usage:
 *   lan866x-gpiomax                         benchmark PA02 (LD1), 3 s, depth 16
 *   lan866x-gpiomax --ip 192.168.0.54
 *   lan866x-gpiomax --pin 6 --secs 5 --depth 8
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

/* Reply tallies, updated by the async callback (runs inline from
 * rcp_async_poll(); single-thread - no locks, no rcp_* calls from here). */
static unsigned g_ok = 0, g_to = 0, g_err = 0;
static int      g_inflight = 0;
static void on_set_done(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx; (void)rx; (void)rxLen;
    if (rc == RT_OK)            g_ok++;
    else if (rc == RT_TIMEOUT)  g_to++;
    else                        g_err++;
    if (g_inflight > 0) g_inflight--;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, pin = 2, secs = 3, depth = 16;
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    uint16_t handle;
    int value = 0;
    unsigned sent = 0;
    DWORD t0, tEnd, now;
    double elapsed, cmdRate, okRate, hz;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-gpiomax - maximum-speed GPIO toggle benchmark over SOME/IP (pure C)\n"
                   "  --pin <0..15>   LED pin PA00..PA15 (default 2 = LD1)\n"
                   "  --secs <s>      measurement window in seconds (default 3)\n"
                   "  --depth <1..16> SetGpio requests kept in flight (default 16)\n"
                   "  --ip/--ep       target endpoint\n"
                   "Toggles the pin as fast as possible and reports commanded vs\n"
                   "confirmed toggle rate. Leaves the LED off on exit.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")      && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin")     && i+1<argc) pin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs")    && i+1<argc) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth")   && i+1<argc) depth = atoi(argv[++i]);
    }
    if (pin < 0 || pin > 15) { printf("--pin must be 0..15.\n"); return 1; }
    if (secs < 1)  secs = 1;
    if (secs > 60) secs = 60;
    if (depth < 1)  depth = 1;
    if (depth > 16) depth = 16;          /* RCP_ASYNC_MAX */

    if (tool_select(wantIp, wantEp, 5, "LAN866x max-speed GPIO toggle benchmark (pure C)") < 0) return 2;

    /* One-time setup (blocking is fine here, not in the hot loop). */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    Sleep(20);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = (uint8_t)pin;
    ov.Direction = 1;                                  /* output, LOW */
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { printf("OpenGpio failed on PA%02d.\n", pin); return 3; }
    handle = orep.HandleGpio;

    /* Short async deadline: a dropped reply frees its pipeline slot quickly so we
     * keep pushing requests at full rate. */
    rcp_set_async_timeout_ms(100);

    printf("\nMax-speed toggle of PA%02d for %d s (pipeline depth %d). Working...\n",
           pin, secs, depth);

    t0   = GetTickCount();
    tEnd = t0 + (DWORD)secs * 1000u;
    do {
        /* Keep the pipeline full: issue SetGpio requests (toggling the value)
         * until `depth` are outstanding or the transmit layer is momentarily
         * busy, then service replies. */
        while (g_inflight < depth) {
            uint8_t p[16];
            uint16_t n = rcp_enc_gpio_set(p, sizeof(p), handle, (uint8_t)(value ^ 1));
            if (!n || rcp_async_request(0x1330u, p, n, on_set_done, NULL) != RT_OK)
                break;                                 /* slot/buffer busy: drain first */
            value ^= 1;
            sent++;
            g_inflight++;
        }
        rcp_async_poll();
        now = GetTickCount();
    } while ((long)(now - tEnd) < 0);

    /* Drain the last in-flight replies (bounded grace). */
    {
        DWORD drainEnd = GetTickCount() + 300u;
        while (g_inflight > 0 && (long)(GetTickCount() - drainEnd) < 0)
            rcp_async_poll();
    }

    elapsed = (double)(now - t0) / 1000.0;
    if (elapsed <= 0.0) elapsed = (double)secs;
    cmdRate = (double)sent  / elapsed;
    okRate  = (double)g_ok  / elapsed;
    hz      = okRate / 2.0;                            /* one square wave = two edges */

    printf("\n==================== GPIO TOGGLE BENCHMARK ====================\n");
    printf("  Pin:             PA%02d   window: %.2f s   depth: %d\n", pin, elapsed, depth);
    printf("  Commanded:       %u toggles  -> %.0f toggles/s\n", sent, cmdRate);
    printf("  Confirmed:       %u toggles  -> %.0f toggles/s  (~%.0f Hz square wave)\n",
           g_ok, okRate, hz);
    printf("  Reply timeouts:  %u   errors: %u", g_to, g_err);
    if (sent) printf("   (reply loss %.0f%%)", 100.0 * (double)(g_to + g_err) / (double)sent);
    printf("\n");
    if (g_to + g_err)
        printf("  Note: on the Windows host, lost replies are usually dropped *replies*\n"
               "        (the toggle still reached the endpoint), not missed toggles.\n");
    printf("===============================================================\n");

    /* Clean exit: LED off + release. */
    {
        SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
        sv.GpioValues[0] = (uint8_t)(handle >> 8);
        sv.GpioValues[1] = (uint8_t)(handle & 0xFF);
        sv.GpioValues[2] = 0;
        sv.GpioValuesLength = 3;
        rcp_set_gpio(&sv);
        rcp_release_digital_pins(&rel);
    }
    return 0;
}
