/*
 * ledpwm.c  -  "Breathing" LED via PWM, non-blocking, over SOME/IP (RCP). Pure C.
 *
 * Smoothly ramps one on-board LED's brightness up and down (a "breathing" effect)
 * by changing the PWM duty cycle in a non-blocking loop (async WritePwm). Where
 * lan866x-ledtoggle does on/off, this shows analog-style dimming via PWM.
 *
 * Default pin PA02 (LD1). The LED is driven through OpenPwm (one-time) + repeated
 * WritePwm to vary the duty cycle.
 *
 *   !! IMPORTANT: PWM support is NOT confirmed on the Lighting firmware (.54).
 *      OpenPwm may return E_UNKNOWN_METHOD / RT_NOT_REACHABLE on a build that does
 *      not implement it. Verify on your board (a quick `lan866x-pwm --pin 2` test
 *      tells you). On a Control build PWM is available. See docs/PWMDEMO.md.
 *
 * Usage:
 *   lan866x-ledpwm --ip 192.168.0.54
 *   lan866x-ledpwm --pin 6 --freq 1000 --period 2000
 */
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static volatile int g_acked = 0;
static void on_write(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{ (void)ctx; (void)rx; (void)rxLen; if (rc == RT_OK) g_acked++; }

/* duty percent (0..100) -> q31 wire value (0 = 0% .. 2^31 = 100%) */
static uint32_t duty_q31(double pct)
{
    if (pct < 0.0) pct = 0.0; if (pct > 100.0) pct = 100.0;
    return (uint32_t)(pct / 100.0 * 2147483648.0);
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, breathMs = 2000, steps = 50;
    uint8_t pin = 2;                 /* LD1 */
    double freq = 1000.0;
    uint32_t periodNs;
    ReleaseDigitalPinsVar_t rel; OpenPwmVar_t ov; OpenPwmReply_t orep;
    uint16_t handle; uint32_t wid = 0; int dir = 1, step = 0;
    DWORD nextStep, stepMs;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ledpwm - breathing LED via PWM, non-blocking (pure C)\n"
                   "  --pin <0..15>   LED pin (default 2 = LD1)\n"
                   "  --freq <Hz>     PWM carrier frequency (default 1000)\n"
                   "  --period <ms>   one breath up+down (default 2000)\n"
                   "  --steps <n>     brightness steps per half-breath (default 50)\n"
                   "  --ip/--ep       target endpoint\n"
                   "NOTE: PWM may be unimplemented on the Lighting firmware (see docs/PWMDEMO.md).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")     && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")       && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin")      && i+1<argc) pin = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--freq")     && i+1<argc) freq = atof(argv[++i]);
        else if (!strcmp(argv[i], "--period")   && i+1<argc) breathMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steps")    && i+1<argc) steps = atoi(argv[++i]);
    }
    if (freq <= 0.0) freq = 1000.0;
    if (steps < 2) steps = 2;
    if (breathMs < 100) breathMs = 100;
    periodNs = (uint32_t)(1e9 / freq + 0.5);
    stepMs = (DWORD)(breathMs / (2 * steps)); if (stepMs < 1) stepMs = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x breathing-LED PWM (non-blocking, pure C)") < 0) return 2;

    /* one-time blocking setup: release the pin, OpenPwm at 0% duty */
    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = pin; rel.PinIdListLength = 1; rcp_release_digital_pins(&rel); Sleep(20);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinId = pin; ov.IntervalTime = periodNs; ov.DutyCycle = 0;
    if (rcp_open_pwm(&ov, &orep) != RT_OK) {
        printf("OpenPwm failed - PWM is likely not implemented on this firmware build.\n"
               "  (Try `lan866x-pwm --pin %u` to confirm; see docs/PWMDEMO.md.)\n", pin);
        return 3;
    }
    handle = orep.HandlePwm;

    rcp_set_async_timeout_ms(200);
    signal(SIGINT, on_sigint);
    printf("\nBreathing LED on PA%02u (%.0f Hz carrier, %d ms/breath). Ctrl+C to stop.\n",
           pin, freq, breathMs);

    /* Non-blocking breathe: a triangle 0%->100%->0% in `steps` increments, each
     * pushed with an async WritePwm; the loop never parks on the round-trip. */
    nextStep = GetTickCount();
    while (g_run) {
        DWORD now = GetTickCount();
        if ((long)(now - nextStep) >= 0) {
            double pct = 100.0 * (double)step / (double)steps;
            uint8_t params[32]; uint16_t n = rcp_enc_pwm_write(params, sizeof(params), handle, wid++, duty_q31(pct));
            if (n) rcp_async_request(0x1804u, params, n, on_write, NULL);   /* returns at once */
            step += dir;
            if (step >= steps) { step = steps; dir = -1; }
            else if (step <= 0) { step = 0; dir = 1; }
            nextStep += stepMs;
        }
        rcp_async_poll();
        Sleep(2);
    }

    /* clean exit: 0% then close */
    {
        uint8_t params[32]; uint16_t n = rcp_enc_pwm_write(params, sizeof(params), handle, wid++, 0);
        ClosePwmVar_t c;
        if (n) { rcp_async_request(0x1804u, params, n, on_write, NULL); rcp_async_poll(); Sleep(20); rcp_async_poll(); }
        memset(&c, 0, sizeof(c)); c.HandlePwm = handle; rcp_close_pwm(&c);
    }
    printf("\nStopped. PWM closed.\n");
    return 0;
}
