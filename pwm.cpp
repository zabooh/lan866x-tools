/*
 * pwm.cpp  -  Drive a PWM output on a LAN866x endpoint (RCP/SOME/IP).
 *
 * Opens a PWM channel on a digital pin with the given frequency and duty
 * cycle. By default the signal is left running on the endpoint after the tool
 * exits (the handle lives on the device). Use --hold to stop it again after a
 * number of seconds.
 *
 * Duty cycle wire encoding: 0 = 0% .. 2^31 = 100% (per OpenPwmVar_t).
 *
 * Usage:
 *   lan866x-pwm --pin 6 --freq 1000 --duty 50     1 kHz, 50% on PA06
 *   lan866x-pwm --pin 6 --period-ns 20000000 --duty 7.5   (servo, 1.5 ms)
 *   lan866x-pwm --pin 6 --duty 0                  stop output (0%)
 *   lan866x-pwm --ip 192.168.0.54 --pin 7 --freq 500 --duty 25 --hold 5
 *
 * Default: PA06, 1 kHz, 50%.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <chrono>
#include "lan866x_client.hpp"

using namespace microchip::rcp;
extern "C" { uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 }; }

static void usage(const char *prog)
{
    printf(
        "%s - drive a PWM output on a LAN866x endpoint\n\n"
        "USAGE:\n"
        "  %s [--ip <addr>|--ep <index>] --pin <n>\n"
        "        [--freq <Hz> | --period-ns <ns>] [--duty <percent>] [--hold <s>]\n\n"
        "OPTIONS:\n"
        "  --pin <0..15>     digital pin PA00..PA15 (default 6)\n"
        "  --freq <Hz>       PWM frequency (default 1000); period = 1e9/freq ns\n"
        "  --period-ns <ns>  period directly in ns (overrides --freq)\n"
        "  --duty <percent>  duty cycle 0..100 (default 50; fractions allowed)\n"
        "  --hold <s>        keep the output for N seconds, then stop (close)\n"
        "                    (default: leave the signal running and exit)\n"
        "  --ip/--ep         target endpoint (default: 0)\n"
        "  -h, --help        this help\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr;
    int wantEp = 0, hold = -1;
    uint8_t pin = 6;
    double freq = 1000.0, duty = 50.0;
    uint32_t periodNs = 0;   /* 0 => derive from freq */

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { usage("lan866x-pwm"); return 0; }
        else if (!strcmp(argv[i],"--ip")        && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i],"--ep")         && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--pin")        && i+1<argc) pin = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--freq")       && i+1<argc) freq = atof(argv[++i]);
        else if (!strcmp(argv[i],"--period-ns")  && i+1<argc) periodNs = (uint32_t)strtoul(argv[++i],nullptr,10);
        else if (!strcmp(argv[i],"--duty")       && i+1<argc) duty = atof(argv[++i]);
        else if (!strcmp(argv[i],"--hold")       && i+1<argc) hold = atoi(argv[++i]);
    }

    if (periodNs == 0) {
        if (freq <= 0.0) { printf("Invalid --freq.\n"); return 1; }
        periodNs = (uint32_t)(1e9 / freq + 0.5);
    }
    if (duty < 0.0) duty = 0.0; if (duty > 100.0) duty = 100.0;
    uint32_t dutyQ31 = (uint32_t)(duty / 100.0 * 2147483648.0);  /* 2^31 = 100% */

    printf("LAN866x PWM tool\nSearching for endpoints (5 s) ...\n");
    for (int i=0;i<50;i++){ (void)LAN866XClientFactory::GetAllClients(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    auto clients = LAN866XClientFactory::GetAllClients();
    if (clients.empty()) { printf("No endpoints found.\n"); return 2; }

    LAN866XClient *ep = nullptr; int idx = 0;
    for (auto *c : clients) {
        uint8_t *ip=nullptr, ipLen=0; uint16_t port=0; c->GetIpAddressAndPort(&ip,&ipLen,&port);
        char s[20]; snprintf(s,sizeof(s),"%u.%u.%u.%u",ip?ip[0]:0,ip?ip[1]:0,ip?ip[2]:0,ip?ip[3]:0);
        if ((wantIp && !strcmp(wantIp,s)) || (!wantIp && idx==wantEp)) ep = c;
        idx++;
    }
    if (!ep) { printf("Target endpoint not found.\n"); return 2; }

    /* release pin, then open PWM */
    ReleaseDigitalPinsVar_t rel; memset(&rel,0,sizeof(rel));
    rel.PinIdList[0] = pin; rel.PinIdListLength = 1; ep->ReleaseDigitalPins(&rel);

    OpenPwmVar_t ov; memset(&ov,0,sizeof(ov));
    ov.PinId = pin; ov.IntervalTime = periodNs; ov.DutyCycle = dutyQ31;
    OpenPwmReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenPwm(&ov,&orep) != RT_OK) { printf("OpenPwm failed (PWM not configured on this pin?).\n"); return 3; }

    printf("\nPWM open: PA%02u  period=%u ns (%.1f Hz)  duty=%.1f%% (q31=%u)\n",
           pin, periodNs, 1e9/(double)periodNs, duty, dutyQ31);

    int rc = 0;
    if (hold >= 0) {
        printf("Holding for %d s, then stopping ...\n", hold);
        std::this_thread::sleep_for(std::chrono::seconds(hold));
        ClosePwmVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandlePwm = orep.HandlePwm; ep->ClosePwm(&cv);
        printf("PWM stopped.\n");
    } else {
        printf("Signal left running on the endpoint. Re-run with --duty 0 or --hold to stop.\n");
    }
    return rc;
}
