/*
 * spi.cpp  -  SPI transfer via a LAN866x endpoint (RCP/SOME/IP).
 *
 * Writes bytes via WriteAndReadSpi and prints the bytes read back at the
 * same time (MISO) (full-duplex, length = number of TX bytes).
 *
 * Usage:
 *   lan866x-spi --tx 9F0000                 3-byte transfer (default pins/mode/speed)
 *   lan866x-spi --tx 0102 --mode 0 --speed 1000000
 *   lan866x-spi --ip 192.168.0.54 --miso 12 --sck 13 --cs 14 --mosi 15 --tx AA55
 *
 * Default pins: MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15, mode 0, 1 MHz.
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
        "%s - SPI transfer via a LAN866x endpoint (full-duplex)\n\n"
        "USAGE:\n"
        "  %s [--ip <addr>|--ep <index>] --tx <hexbytes>\n"
        "        [--miso n][--sck n][--cs n][--mosi n][--mode 0..3][--speed Hz]\n\n"
        "OPTIONS:\n"
        "  --tx <hex>    bytes to send as hex (e.g. 9F0000); RX length = TX length\n"
        "  --ip/--ep     target endpoint (default: 0)\n"
        "  --miso/--sck/--cs/--mosi  pins PA0..15 (default 12/13/14/15; 255=unused)\n"
        "  --mode <0..3> SPI mode (default 0)\n"
        "  --speed <Hz>  clock rate, e.g. 1000000 (default)\n"
        "  -h, --help    this help\n",
        prog, prog);
}

static int hex2bytes(const char *s, uint8_t *out, int maxlen)
{
    int n = 0; for (; s[0] && s[1] && n < maxlen; s += 2) {
        char b[3] = { s[0], s[1], 0 }; char *e=nullptr;
        long v = strtol(b,&e,16); if (*e) return -1; out[n++] = (uint8_t)v;
    }
    return s[0] ? -1 : n;   /* odd number of digits -> error */
}

int main(int argc, char **argv)
{
    const char *wantIp = nullptr, *txHex = nullptr;
    int wantEp = 0; uint8_t miso=12, sck=13, cs=14, mosi=15, mode=0; uint32_t speed=1000000;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")) { usage("lan866x-spi"); return 0; }
        else if (!strcmp(argv[i],"--ip")   && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i],"--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--tx")    && i+1<argc) txHex = argv[++i];
        else if (!strcmp(argv[i],"--miso")  && i+1<argc) miso = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--sck")   && i+1<argc) sck  = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--cs")    && i+1<argc) cs   = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--mosi")  && i+1<argc) mosi = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--mode")  && i+1<argc) mode = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i],"--speed") && i+1<argc) speed = (uint32_t)strtoul(argv[++i],nullptr,10);
    }
    if (!txHex) { usage("lan866x-spi"); return 1; }

    uint8_t tx[1400]; int txLen = hex2bytes(txHex, tx, sizeof(tx));
    if (txLen <= 0) { printf("Invalid --tx hex data (even number of hex digits required).\n"); return 1; }

    printf("LAN866x SPI tool\nSearching for endpoints (5 s) ...\n");
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

    /* release pins */
    ReleaseDigitalPinsVar_t rel; memset(&rel,0,sizeof(rel)); uint16_t p=0;
    for (uint8_t pin : {miso,sck,cs,mosi}) if (pin != 0xFF) rel.PinIdList[p++] = pin;
    rel.PinIdListLength = p; ep->ReleaseDigitalPins(&rel);

    /* open SPI */
    OpenSpiVar_t ov; memset(&ov,0,sizeof(ov));
    ov.PinIdMiso=miso; ov.PinIdSck=sck; ov.PinIdCs=cs; ov.PinIdMosi=mosi; ov.Mode=mode; ov.ClockSpeed=speed;
    OpenSpiReply_t orep; memset(&orep,0,sizeof(orep));
    if (ep->OpenSpi(&ov,&orep) != RT_OK) { printf("OpenSpi failed. Check pins/speed.\n"); return 3; }
    printf("\nSPI open: MISO=PA%02u SCK=PA%02u CS=PA%02u MOSI=PA%02u mode%u @ %u Hz\n",
           miso,sck,cs,mosi,mode,speed);

    /* transfer */
    WriteAndReadSpiVar_t wr; memset(&wr,0,sizeof(wr));
    wr.HandleSpi = orep.HandleSpi; wr.WriteId = 0; wr.ReadDataLength = (uint16_t)txLen;
    wr.WriteDataLength = (uint16_t)txLen; memcpy(wr.WriteData, tx, txLen);
    WriteAndReadSpiReply_t rr; memset(&rr,0,sizeof(rr));

    int rc = 0;
    if (ep->WriteAndReadSpi(&wr,&rr) == RT_OK) {
        printf("TX:"); for (int i=0;i<txLen;i++) printf(" %02X", tx[i]);
        printf("\nRX:"); for (uint16_t i=0;i<rr.ReadDataLength;i++) printf(" %02X", rr.ReadData[i]);
        printf("\n");
    } else { printf("WriteAndReadSpi failed.\n"); rc = 4; }

    CloseSpiVar_t cv; memset(&cv,0,sizeof(cv)); cv.HandleSpi = orep.HandleSpi; ep->CloseSpi(&cv);
    return rc;
}
