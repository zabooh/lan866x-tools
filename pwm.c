/*
 * pwm.c  -  Drive a PWM output on a LAN866x endpoint. Pure C.
 *
 * Duty cycle wire encoding: 0 = 0% .. 2^31 = 100% (per OpenPwmVar_t).
 *
 * Usage:
 *   lan866x-pwm --pin 6 --freq 1000 --duty 50
 *   lan866x-pwm --pin 6 --period-ns 20000000 --duty 7.5   (servo, 1.5 ms)
 *   lan866x-pwm --pin 6 --duty 0                          stop output (0%)
 *   lan866x-pwm --pin 7 --freq 500 --duty 25 --hold 5
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, hold = -1;
    uint8_t pin = 6;
    double freq = 1000.0, duty = 50.0;
    uint32_t periodNs = 0, dutyQ31;
    ReleaseDigitalPinsVar_t rel; OpenPwmVar_t ov; OpenPwmReply_t orep;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-pwm - drive a PWM output (pure C)\n"
                   "  --pin <0..15>     digital pin (default 6)\n"
                   "  --freq <Hz>       frequency (default 1000)  | --period-ns <ns>\n"
                   "  --duty <percent>  0..100 (default 50)\n"
                   "  --hold <s>        keep N seconds then stop (default: leave running)\n"
                   "  --ip/--ep         target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")        && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")        && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin")       && i+1<argc) pin = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--freq")      && i+1<argc) freq = atof(argv[++i]);
        else if (!strcmp(argv[i], "--period-ns") && i+1<argc) periodNs = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--duty")      && i+1<argc) duty = atof(argv[++i]);
        else if (!strcmp(argv[i], "--hold")      && i+1<argc) hold = atoi(argv[++i]);
    }
    if (periodNs == 0) {
        if (freq <= 0.0) { printf("Invalid --freq.\n"); return 1; }
        periodNs = (uint32_t)(1e9 / freq + 0.5);
    }
    if (duty < 0.0) duty = 0.0;
    if (duty > 100.0) duty = 100.0;
    dutyQ31 = (uint32_t)(duty / 100.0 * 2147483648.0);

    if (tool_select(wantIp, wantEp, 5, "LAN866x PWM tool (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = pin; rel.PinIdListLength = 1; rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinId = pin; ov.IntervalTime = periodNs; ov.DutyCycle = dutyQ31;
    if (rcp_open_pwm(&ov, &orep) != RT_OK) { printf("OpenPwm failed (PWM not configured on this pin?).\n"); return 3; }

    printf("\nPWM open: PA%02u  period=%u ns (%.1f Hz)  duty=%.1f%% (q31=%u)\n",
           pin, periodNs, 1e9 / (double)periodNs, duty, dutyQ31);

    if (hold >= 0) {
        ClosePwmVar_t cv;
        printf("Holding for %d s, then stopping ...\n", hold);
        Sleep((DWORD)hold * 1000);
        memset(&cv, 0, sizeof(cv)); cv.HandlePwm = orep.HandlePwm; rcp_close_pwm(&cv);
        printf("PWM stopped.\n");
    } else {
        printf("Signal left running on the endpoint. Re-run with --duty 0 or --hold to stop.\n");
    }
    return 0;
}
