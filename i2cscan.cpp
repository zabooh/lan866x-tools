/*
 * i2cscan.cpp  -  I2C bus scanner via a LAN866x endpoint (RCP/SOME/IP), like i2cdetect.
 *
 * Opens the I2C interface of an endpoint and probes the 7-bit addresses
 * 0x08..0x77 with a 1-byte read. Addresses that ACK are shown as present.
 *
 * Usage:
 *   i2cscan                         first endpoint, SDA=PA08 SCL=PA09, 400 kHz
 *   i2cscan --ip 192.168.0.54       select endpoint by IP
 *   i2cscan --ep 1                  select endpoint by index
 *   i2cscan --sda 8 --scl 9         set pins (PA number, 0..15)
 *   i2cscan --speed 0|1|2           0=100kHz 1=400kHz 2=1MHz
 *
 * Note: pins must match the board configuration and be free.
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
        "%s - I2C bus scanner via a LAN866x endpoint (like i2cdetect)\n\n"
        "Opens an endpoint's I2C interface and probes 0x08..0x77.\n\n"
        "USAGE:\n"
        "  %s [--ip <addr>|--ep <index>] [--sda <0..15>] [--scl <0..15>] [--speed 0|1|2]\n\n"
        "OPTIONS:\n"
        "  --ip <addr>    select target endpoint by IP (e.g. 192.168.0.54)\n"
        "  --ep <index>   select target endpoint by list index (default: 0)\n"
        "  --sda <pin>    SDA pin as PA number 0..15 (default: 8 = PA08)\n"
        "  --scl <pin>    SCL pin as PA number 0..15 (default: 9 = PA09)\n"
        "  --speed <n>    0 = 100 kHz, 1 = 400 kHz (default), 2 = 1 MHz\n"
        "  -h, --help     show this help\n\n"
        "Endpoints are found via SOME/IP Service Discovery. Pins must match the\n"
        "board configuration; SDA/SCL are released before OpenI2C.\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr;
    int wantEp = 0;
    uint8_t sda = 8, scl = 9, speed = 1;   /* PA08/PA09, 400 kHz (default, like demo board) */

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { usage("lan866x-i2cscan"); return 0; }
        else if (!strcmp(argv[i], "--ip")  && i+1 < argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1 < argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1 < argc) sda = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1 < argc) scl = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1 < argc) speed = (uint8_t)atoi(argv[++i]);
    }

    printf("LAN866x I2C bus scanner\n");
    printf("Searching for endpoints (5 s) ...\n");
    for (int i = 0; i < 50; ++i) {
        (void)LAN866XClientFactory::GetAllClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto clients = LAN866XClientFactory::GetAllClients();
    if (clients.empty()) { printf("No endpoints found.\n"); return 2; }

    /* --- select target endpoint --- */
    LAN866XClient *ep = nullptr;
    int idx = 0;
    for (auto *c : clients) {
        uint8_t *ip=nullptr, ipLen=0; uint16_t port=0;
        c->GetIpAddressAndPort(&ip,&ipLen,&port);
        char ipstr[20];
        snprintf(ipstr,sizeof(ipstr),"%u.%u.%u.%u",ip?ip[0]:0,ip?ip[1]:0,ip?ip[2]:0,ip?ip[3]:0);
        if ((wantIp && !strcmp(wantIp, ipstr)) || (!wantIp && idx == wantEp)) { ep = c; }
        printf("  [%d] %s%s\n", idx, ipstr, (c==ep)?"  <== target":"");
        idx++;
    }
    if (!ep) { printf("Target endpoint not found.\n"); return 2; }

    /* --- release SDA/SCL pins (otherwise OpenI2C fails) --- */
    ReleaseDigitalPinsVar_t rel; memset(&rel,0,sizeof(rel));
    rel.PinIdList[0] = sda; rel.PinIdList[1] = scl; rel.PinIdListLength = 2;
    ep->ReleaseDigitalPins(&rel);   /* best effort */

    /* --- open I2C --- */
    OpenI2CVar_t ov; memset(&ov,0,sizeof(ov));
    ov.PinIdSda = sda; ov.PinIdScl = scl; ov.ClockSpeed = speed;
    OpenI2CReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenI2C(&ov, &orep) != RT_OK) {
        printf("OpenI2C failed (SDA=PA%02u SCL=PA%02u). Check pins/board config.\n", sda, scl);
        return 3;
    }
    const char *spd = speed==0?"100 kHz":speed==1?"400 kHz":speed==2?"1 MHz":"?";
    printf("\nI2C open: SDA=PA%02u SCL=PA%02u @ %s  (handle 0x%04X)\n", sda, scl, spd, orep.HandleI2C);
    printf("Scanning addresses 0x08..0x77 ...\n\n");

    /* --- probe addresses 0x08..0x77 (1-byte read) --- */
    int found = 0;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (int row = 0; row < 0x80; row += 0x10) {
        printf("%02x:", row);
        for (int col = 0; col < 0x10; ++col) {
            int addr = row + col;
            if (addr < 0x08 || addr > 0x77) { printf("   "); continue; }
            ReadI2CVar_t rd; memset(&rd,0,sizeof(rd));
            rd.HandleI2C = orep.HandleI2C;
            rd.DeviceAddress = (uint16_t)addr;   /* 7-bit address, no R/W bit */
            rd.ReadDataLength = 1;
            ReadI2CReply_t rrep; memset(&rrep,0,sizeof(rrep));
            if (ep->ReadI2C(&rd, &rrep) == RT_OK) { printf(" %02x", addr); found++; }
            else                                   printf(" --");
        }
        printf("\n");
    }

    /* --- close I2C --- */
    CloseI2CVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandleI2C = orep.HandleI2C;
    ep->CloseI2C(&cv);

    printf("\n%d device(s) found on the I2C bus.\n", found);
    return 0;
}
