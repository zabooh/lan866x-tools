/*
 * ledtoggle.c  -  Non-blocking single-LED toggle over SOME/IP (RCP). Pure C.
 *
 * Toggles ONE on-board LED at a half-second beat (on 500 ms, off 500 ms, ...)
 * WITHOUT ever blocking the main loop on the network. Where `lan866x-ledblink`
 * calls the blocking rcp_set_gpio() (each call waits for the endpoint's reply),
 * this tool fires the SetGpio request with the ASYNC API and keeps spinning; the
 * reply is delivered later to a short callback from rcp_async_poll().
 *
 * This is the pattern an MCU superloop wants: the loop must stay responsive (do
 * other work, service other I/O) while a control round-trip is in flight.
 *
 * The LED defaults to PA02 (LD1, see led_map.json / docs/LEDDEMO.md).
 *
 * Usage:
 *   lan866x-ledtoggle                       toggle PA02 (LD1) @ 500 ms
 *   lan866x-ledtoggle --ip 192.168.0.54
 *   lan866x-ledtoggle --pin 6 --beat 250
 */
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

/* Reply callback: runs inline from rcp_async_poll() (single-thread). Keep it
 * short and do NOT call rcp_* from here. We just tally outcomes. */
static unsigned g_ok = 0, g_to = 0, g_err = 0;
static void on_set_done(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx; (void)rx; (void)rxLen;
    if (rc == RT_OK)            g_ok++;
    else if (rc == RT_TIMEOUT)  g_to++;
    else                        g_err++;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, pin = 2, beat = 500;
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    uint16_t handle;
    int value = 0;
    unsigned long loops = 0, loopsAtLastToggle = 0;
    DWORD nextToggle;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ledtoggle - non-blocking single-LED toggle over SOME/IP (pure C)\n"
                   "  --pin <0..15>   LED pin PA00..PA15 (default 2 = LD1)\n"
                   "  --beat <ms>     toggle interval (default 500)\n"
                   "  --ip/--ep       target endpoint\n"
                   "Runs forever; Ctrl+C turns the LED off and exits.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")   && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")     && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin")    && i+1<argc) pin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--beat")   && i+1<argc) beat = atoi(argv[++i]);
    }
    if (pin < 0 || pin > 15) { printf("--pin must be 0..15.\n"); return 1; }
    if (beat < 1) beat = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x non-blocking LED toggle (pure C)") < 0) return 2;

    /* One-time setup may use the simple blocking calls (not in the hot loop). */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    Sleep(20);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = (uint8_t)pin;
    ov.Direction = 1;                                  /* output, LOW */
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { printf("OpenGpio failed on PA%02d.\n", pin); return 3; }
    handle = orep.HandleGpio;

    /* Async deadline shorter than the beat, so a lost reply times out (and the
     * callback tallies it) before the next toggle is due. */
    rcp_set_async_timeout_ms(300);

    signal(SIGINT, on_sigint);
    printf("\nNon-blocking toggle of PA%02d every %d ms. Press Ctrl+C to stop.\n", pin, beat);
    printf("(The loop never blocks on the network - note how many times it spins\n"
           " between two toggles.)\n\n");

    nextToggle = GetTickCount();
    while (g_run) {
        DWORD now = GetTickCount();

        /* Time to toggle? Fire the SetGpio async and return immediately. */
        if ((long)(now - nextToggle) >= 0) {
            uint8_t p[16];
            uint16_t n;
            value = !value;
            n = rcp_enc_gpio_set(p, sizeof(p), handle, (uint8_t)value);
            if (n) rcp_async_request(0x1330u, p, n, on_set_done, NULL);
            nextToggle += (DWORD)beat;

            printf("  PA%02d -> %-3s  (loop spun %lu times since last toggle; ok=%u to=%u err=%u)\n",
                   pin, value ? "ON" : "OFF", loops - loopsAtLastToggle, g_ok, g_to, g_err);
            loopsAtLastToggle = loops;
        }

        /* Non-blocking: drives replies + deadlines. The callback runs from here. */
        rcp_async_poll();

        /* In a real app this is where the rest of the superloop runs. We just
         * yield a little so we don't busy-spin a whole CPU core. */
        Sleep(2);
        loops++;
    }

    /* Clean exit: LED off + release (one-time, blocking is fine here). */
    {
        SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
        sv.GpioValues[0] = (uint8_t)(handle >> 8);
        sv.GpioValues[1] = (uint8_t)(handle & 0xFF);
        sv.GpioValues[2] = 0;
        sv.GpioValuesLength = 3;
        rcp_set_gpio(&sv);
        rcp_release_digital_pins(&rel);
    }
    printf("\nStopped. PA%02d off.\n", pin);
    return 0;
}
