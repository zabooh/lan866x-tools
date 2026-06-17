/*
 * gpio.cpp  -  GPIO setzen/lesen an einem LAN866x-Endpoint (RCP/SOME/IP).
 *
 * Aufruf:
 *   lan866x-gpio --pin 2 --set 1        Pin PA02 als Output, auf High setzen
 *   lan866x-gpio --pin 2 --set 0        Pin PA02 auf Low
 *   lan866x-gpio --pin 2 --get          Pin PA02 als Input lesen
 *   lan866x-gpio --ip 192.168.0.54 --pin 6 --set 1
 *
 * Endpoint-Wahl wie bei den anderen Tools (--ip / --ep, Default 0).
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
        "%s - GPIO setzen/lesen an einem LAN866x-Endpoint\n\n"
        "AUFRUF:\n"
        "  %s [--ip <addr>|--ep <index>] --pin <0..15> (--set <0|1> | --get)\n\n"
        "OPTIONEN:\n"
        "  --ip <addr>   Ziel-Endpoint per IP\n"
        "  --ep <index>  Ziel-Endpoint per Index (Default: 0)\n"
        "  --pin <n>     GPIO-Pin als PA-Nummer 0..15\n"
        "  --set <0|1>   Pin als Output, auf Low/High setzen\n"
        "  --get         Pin als Input lesen\n"
        "  -h, --help    diese Hilfe\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr;
    int wantEp = 0, pin = -1, setVal = -1; bool doGet = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { usage("lan866x-gpio"); return 0; }
        else if (!strcmp(argv[i],"--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i],"--ep")   && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--pin")  && i+1<argc) pin = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--set")  && i+1<argc) setVal = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--get")) doGet = true;
    }
    if (pin < 0 || pin > 15 || (setVal < 0 && !doGet)) { usage("lan866x-gpio"); return 1; }

    printf("LAN866x GPIO-Tool\nSuche Endpoints (5 s) ...\n");
    for (int i=0;i<50;i++){ (void)LAN866XClientFactory::GetAllClients(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    auto clients = LAN866XClientFactory::GetAllClients();
    if (clients.empty()) { printf("Keine Endpoints gefunden.\n"); return 2; }

    LAN866XClient *ep = nullptr; int idx = 0;
    for (auto *c : clients) {
        uint8_t *ip=nullptr, ipLen=0; uint16_t port=0; c->GetIpAddressAndPort(&ip,&ipLen,&port);
        char s[20]; snprintf(s,sizeof(s),"%u.%u.%u.%u",ip?ip[0]:0,ip?ip[1]:0,ip?ip[2]:0,ip?ip[3]:0);
        if ((wantIp && !strcmp(wantIp,s)) || (!wantIp && idx==wantEp)) ep = c;
        idx++;
    }
    if (!ep) { printf("Ziel-Endpoint nicht gefunden.\n"); return 2; }

    /* Pin entsperren */
    ReleaseDigitalPinsVar_t rel; memset(&rel,0,sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1; ep->ReleaseDigitalPins(&rel);

    /* GPIO öffnen: Output (1=low) zum Setzen, Input (0) zum Lesen */
    OpenGpioVar_t ov; memset(&ov,0,sizeof(ov));
    ov.PinIdGpio = (uint8_t)pin; ov.Direction = doGet ? 0 : 1;
    OpenGpioReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenGpio(&ov,&orep) != RT_OK) { printf("OpenGpio(PA%02d) fehlgeschlagen.\n",pin); return 3; }
    uint16_t handle = orep.HandleGpio;

    int rc = 0;
    if (!doGet) {
        SetGpioVar_t sv; memset(&sv,0,sizeof(sv));
        sv.GpioValues[0] = (uint8_t)(handle >> 8);   /* uint16 Handle, big-endian */
        sv.GpioValues[1] = (uint8_t)handle;
        sv.GpioValues[2] = (uint8_t)(setVal ? 1 : 0);
        sv.GpioValuesLength = 3;
        if (ep->SetGpio(&sv) == RT_OK) printf("PA%02d = %d (gesetzt)\n", pin, setVal?1:0);
        else { printf("SetGpio fehlgeschlagen.\n"); rc = 4; }
    } else {
        GetGpioReply_t gr; memset(&gr,0,sizeof(gr));
        if (ep->GetGpio(&gr) == RT_OK) {
            int val = -1;
            for (uint16_t i=0;i+2<gr.GpioValuesLength;i+=3) {
                uint16_t h=(gr.GpioValues[i]<<8)|gr.GpioValues[i+1];
                if (h==handle) val=gr.GpioValues[i+2];
            }
            if (val>=0) printf("PA%02d = %d (gelesen)\n", pin, val);
            else { printf("Pin-Handle nicht in Antwort.\n"); rc=4; }
        } else { printf("GetGpio fehlgeschlagen.\n"); rc=4; }
    }

    CloseGpioVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandleGpio = handle; ep->CloseGpio(&cv);
    return rc;
}
