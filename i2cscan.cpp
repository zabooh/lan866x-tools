/*
 * i2cscan.cpp  -  I2C-Bus-Scanner über einen LAN866x-Endpoint (à la i2cdetect).
 *
 * Öffnet die I2C-Schnittstelle eines Endpoints (RCP/SOME/IP) und probt die
 * 7-Bit-Adressen 0x08..0x77 per 1-Byte-Read. Adressen, die mit ACK antworten,
 * werden als belegt angezeigt.
 *
 * Aufruf:
 *   i2cscan                         erster Endpoint, SDA=PA04 SCL=PA05, 400 kHz
 *   i2cscan --ip 192.168.0.54       Endpoint per IP wählen
 *   i2cscan --ep 1                  Endpoint per Index wählen
 *   i2cscan --sda 4 --scl 5         Pins setzen (PA-Nummer, 0..15)
 *   i2cscan --speed 0|1|2           0=100kHz 1=400kHz 2=1MHz
 *
 * Hinweis: Pins müssen zur Board-Konfiguration passen und frei sein.
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
        "%s - I2C-Bus-Scanner ueber einen LAN866x-Endpoint (à la i2cdetect)\n\n"
        "Oeffnet die I2C-Schnittstelle eines Endpoints und probt 0x08..0x77.\n\n"
        "AUFRUF:\n"
        "  %s [--ip <addr>|--ep <index>] [--sda <0..15>] [--scl <0..15>] [--speed 0|1|2]\n\n"
        "OPTIONEN:\n"
        "  --ip <addr>    Ziel-Endpoint per IP waehlen (z. B. 192.168.0.54)\n"
        "  --ep <index>   Ziel-Endpoint per Listen-Index (Default: 0)\n"
        "  --sda <pin>    SDA-Pin als PA-Nummer 0..15 (Default: 8 = PA08)\n"
        "  --scl <pin>    SCL-Pin als PA-Nummer 0..15 (Default: 9 = PA09)\n"
        "  --speed <n>    0 = 100 kHz, 1 = 400 kHz (Default), 2 = 1 MHz\n"
        "  -h, --help     diese Hilfe anzeigen\n\n"
        "Endpoints werden per SOME/IP Service Discovery gefunden. Pins muessen zur\n"
        "Board-Konfiguration passen; SDA/SCL werden vor OpenI2C entsperrt.\n",
        prog, prog);
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr;
    int wantEp = 0;
    uint8_t sda = 8, scl = 9, speed = 1;   /* PA08/PA09, 400 kHz (Default, wie Demo-Board) */

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) { usage("lan866x-i2cscan"); return 0; }
        else if (!strcmp(argv[i], "--ip")  && i+1 < argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1 < argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1 < argc) sda = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1 < argc) scl = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1 < argc) speed = (uint8_t)atoi(argv[++i]);
    }

    printf("LAN866x I2C-Bus-Scanner\n");
    printf("Suche Endpoints (5 s) ...\n");
    for (int i = 0; i < 50; ++i) {
        (void)LAN866XClientFactory::GetAllClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto clients = LAN866XClientFactory::GetAllClients();
    if (clients.empty()) { printf("Keine Endpoints gefunden.\n"); return 2; }

    /* --- Ziel-Endpoint wählen --- */
    LAN866XClient *ep = nullptr;
    int idx = 0;
    for (auto *c : clients) {
        uint8_t *ip=nullptr, ipLen=0; uint16_t port=0;
        c->GetIpAddressAndPort(&ip,&ipLen,&port);
        char ipstr[20];
        snprintf(ipstr,sizeof(ipstr),"%u.%u.%u.%u",ip?ip[0]:0,ip?ip[1]:0,ip?ip[2]:0,ip?ip[3]:0);
        if ((wantIp && !strcmp(wantIp, ipstr)) || (!wantIp && idx == wantEp)) { ep = c; }
        printf("  [%d] %s%s\n", idx, ipstr, (c==ep)?"  <== Ziel":"");
        idx++;
    }
    if (!ep) { printf("Ziel-Endpoint nicht gefunden.\n"); return 2; }

    /* --- SDA/SCL-Pins entsperren (sonst schlägt OpenI2C fehl) --- */
    ReleaseDigitalPinsVar_t rel; memset(&rel,0,sizeof(rel));
    rel.PinIdList[0] = sda; rel.PinIdList[1] = scl; rel.PinIdListLength = 2;
    ep->ReleaseDigitalPins(&rel);   /* best effort */

    /* --- I2C öffnen --- */
    OpenI2CVar_t ov; memset(&ov,0,sizeof(ov));
    ov.PinIdSda = sda; ov.PinIdScl = scl; ov.ClockSpeed = speed;
    OpenI2CReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenI2C(&ov, &orep) != RT_OK) {
        printf("OpenI2C fehlgeschlagen (SDA=PA%02u SCL=PA%02u). Pins/Board-Konfig prüfen.\n", sda, scl);
        return 3;
    }
    const char *spd = speed==0?"100 kHz":speed==1?"400 kHz":speed==2?"1 MHz":"?";
    printf("\nI2C offen: SDA=PA%02u SCL=PA%02u @ %s  (Handle 0x%04X)\n", sda, scl, spd, orep.HandleI2C);
    printf("Scanne Adressen 0x08..0x77 ...\n\n");

    /* --- Adressen 0x08..0x77 proben (1-Byte-Read) --- */
    int found = 0;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (int row = 0; row < 0x80; row += 0x10) {
        printf("%02x:", row);
        for (int col = 0; col < 0x10; ++col) {
            int addr = row + col;
            if (addr < 0x08 || addr > 0x77) { printf("   "); continue; }
            ReadI2CVar_t rd; memset(&rd,0,sizeof(rd));
            rd.HandleI2C = orep.HandleI2C;
            rd.DeviceAddress = (uint16_t)addr;   /* 7-Bit-Adresse, ohne R/W-Bit */
            rd.ReadDataLength = 1;
            ReadI2CReply_t rrep; memset(&rrep,0,sizeof(rrep));
            if (ep->ReadI2C(&rd, &rrep) == RT_OK) { printf(" %02x", addr); found++; }
            else                                   printf(" --");
        }
        printf("\n");
    }

    /* --- I2C schließen --- */
    CloseI2CVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandleI2C = orep.HandleI2C;
    ep->CloseI2C(&cv);

    printf("\n%d Gerät(e) auf dem I2C-Bus gefunden.\n", found);
    return 0;
}
