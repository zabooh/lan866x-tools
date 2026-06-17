/*
 * adc.cpp  -  Read the on-chip ADC of a LAN866x endpoint (RCP/SOME/IP).
 *
 * Opens the ADC, reads the 12-bit value (0..4095) and prints it together with
 * the scaled voltage. Can read the analog input or the internal temperature
 * sensor, and can poll repeatedly.
 *
 * Usage:
 *   lan866x-adc                          single analog read, 3V3 reference
 *   lan866x-adc --temp                   read internal temperature sensor
 *   lan866x-adc --vref 1                 use the 1V1 reference
 *   lan866x-adc --count 10 --interval 200
 *   lan866x-adc --ip 192.168.0.54
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
        "%s - read the on-chip ADC of a LAN866x endpoint\n\n"
        "USAGE:\n"
        "  %s [--ip <addr>|--ep <index>] [--channel 0|1] [--vref 0|1]\n"
        "        [--temp] [--count N] [--interval ms]\n\n"
        "OPTIONS:\n"
        "  --channel <0|1>  0 = analog input (default), 1 = internal temperature\n"
        "  --temp           shortcut for --channel 1\n"
        "  --vref <0|1>     0 = 3V3 / VDDA33 (default), 1 = 1V1\n"
        "  --count <N>      number of reads (default 1; 0 = endless)\n"
        "  --interval <ms>  delay between reads (default 200)\n"
        "  --ip/--ep        target endpoint (default: 0)\n"
        "  -h, --help       this help\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr;
    int wantEp = 0, count = 1, interval = 200;
    uint8_t channel = 0, vref = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { usage("lan866x-adc"); return 0; }
        else if (!strcmp(argv[i],"--ip")       && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i],"--ep")        && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--channel")   && i+1<argc) channel = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--temp"))                  channel = 1;
        else if (!strcmp(argv[i],"--vref")      && i+1<argc) vref = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--count")     && i+1<argc) count = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--interval")  && i+1<argc) interval = atoi(argv[++i]);
    }

    printf("LAN866x ADC tool\nSearching for endpoints (5 s) ...\n");
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

    /* open ADC (PinId is the analog pin, always 0) */
    OpenAdcVar_t ov; memset(&ov,0,sizeof(ov)); ov.PinId = 0;
    OpenAdcReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenAdc(&ov,&orep) != RT_OK) { printf("OpenAdc failed (ADC not configured on this endpoint?).\n"); return 3; }

    const double vfull = (vref == 1) ? 1.1 : 3.3;
    printf("\nADC open: %s, reference %s\n",
           channel == 1 ? "internal temperature" : "analog input",
           vref == 1 ? "1V1" : "3V3");

    int rc = 0;
    for (int n = 0; count == 0 || n < count; ++n) {
        ReadAdcVar_t rv; memset(&rv,0,sizeof(rv));
        rv.HandleAdc = orep.HandleAdc; rv.ChannelSelecct = channel; rv.VoltageReference = vref;
        ReadAdcReply_t rr; memset(&rr,0,sizeof(rr));
        if (ep->ReadAdc(&rv,&rr) == RT_OK) {
            double volt = (double)rr.ReadData / 4095.0 * vfull;
            if (channel == 1)
                printf("  raw=%4u  (internal temperature sensor)\n", rr.ReadData);
            else
                printf("  raw=%4u  =  %.3f V\n", rr.ReadData, volt);
        } else { printf("ReadAdc failed.\n"); rc = 4; break; }
        if (count == 0 || n+1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }

    CloseAdcVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandleAdc = orep.HandleAdc; ep->CloseAdc(&cv);
    return rc;
}
