/*
 * spi.c  -  SPI full-duplex transfer via a LAN866x endpoint. Pure C.
 *
 * Usage:
 *   lan866x-spi --tx 9F0000
 *   lan866x-spi --tx 0102 --mode 0 --speed 1000000
 *   lan866x-spi --ip 192.168.0.54 --miso 12 --sck 13 --cs 14 --mosi 15 --tx AA55
 *
 * Default pins: MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15, mode 0, 1 MHz.
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static int hex2bytes(const char *s, uint8_t *out, int maxlen)
{
    int n = 0;
    for (; s[0] && s[1] && n < maxlen; s += 2) {
        char b[3]; char *e = NULL; long v;
        b[0] = s[0]; b[1] = s[1]; b[2] = 0;
        v = strtol(b, &e, 16);
        if (*e) return -1;
        out[n++] = (uint8_t)v;
    }
    return s[0] ? -1 : n;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL, *txHex = NULL;
    int wantEp = 0, i, txLen;
    uint8_t miso = 12, sck = 13, cs = 14, mosi = 15, mode = 0, tx[1400];
    uint32_t speed = 1000000;
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    WriteAndReadSpiVar_t wr; WriteAndReadSpiReply_t rr; CloseSpiVar_t cv;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-spi - SPI full-duplex transfer (pure C)\n"
                   "  --tx <hex>   bytes to send (RX length = TX length)\n"
                   "  --miso/--sck/--cs/--mosi pins (default 12/13/14/15; 255=unused)\n"
                   "  --mode 0..3  --speed Hz   --ip/--ep target\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tx")    && i+1<argc) txHex = argv[++i];
        else if (!strcmp(argv[i], "--miso")  && i+1<argc) miso = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sck")   && i+1<argc) sck  = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cs")    && i+1<argc) cs   = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mosi")  && i+1<argc) mosi = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode")  && i+1<argc) mode = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1<argc) speed = (uint32_t)strtoul(argv[++i], NULL, 10);
    }
    if (!txHex) { printf("Need --tx <hexbytes>. Use --help.\n"); return 1; }
    txLen = hex2bytes(txHex, tx, sizeof(tx));
    if (txLen <= 0) { printf("Invalid --tx hex (even number of digits required).\n"); return 1; }

    if (tool_select(wantIp, wantEp, 5, "LAN866x SPI tool (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    { uint8_t pins[4] = { miso, sck, cs, mosi }; uint16_t p = 0;
      for (i = 0; i < 4; ++i) if (pins[i] != 0xFF) rel.PinIdList[p++] = pins[i];
      rel.PinIdListLength = (uint16_t)p; }
    rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdMiso = miso; ov.PinIdSck = sck; ov.PinIdCs = cs; ov.PinIdMosi = mosi;
    ov.Mode = mode; ov.ClockSpeed = speed;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { printf("OpenSpi failed. Check pins/speed.\n"); return 3; }
    printf("\nSPI open: MISO=PA%02u SCK=PA%02u CS=PA%02u MOSI=PA%02u mode%u @ %u Hz\n",
           miso, sck, cs, mosi, mode, speed);

    memset(&wr, 0, sizeof(wr)); memset(&rr, 0, sizeof(rr));
    wr.HandleSpi = orep.HandleSpi; wr.ReadDataLength = (uint16_t)txLen; wr.WriteId = 0;
    wr.WriteDataLength = (uint16_t)txLen; memcpy(wr.WriteData, tx, txLen);

    if (rcp_write_and_read_spi(&wr, &rr) == RT_OK) {
        printf("TX:"); for (i = 0; i < txLen; ++i) printf(" %02X", tx[i]);
        printf("\nRX:"); for (i = 0; i < (int)rr.ReadDataLength; ++i) printf(" %02X", rr.ReadData[i]);
        printf("\n");
    } else { printf("WriteAndReadSpi failed.\n"); }

    memset(&cv, 0, sizeof(cv)); cv.HandleSpi = orep.HandleSpi; rcp_close_spi(&cv);
    return 0;
}
