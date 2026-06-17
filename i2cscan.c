/*
 * i2cscan.c  -  Scan the I2C bus of a LAN866x endpoint (like i2cdetect). Pure C.
 *
 * Usage:
 *   lan866x-i2cscan                       first endpoint, SDA=PA08 SCL=PA09, 400 kHz
 *   lan866x-i2cscan --ip 192.168.0.54
 *   lan866x-i2cscan --ep 1 --sda 8 --scl 9 --speed 1
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, found = 0;
    uint8_t sda = 8, scl = 9, speed = 1;
    OpenI2CVar_t ov; OpenI2CReply_t orep; ReleaseDigitalPinsVar_t rel;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-i2cscan - scan an endpoint's I2C bus (pure C)\n"
                   "  --ip/--ep   target endpoint\n  --sda/--scl pins (default 8/9)\n"
                   "  --speed     0=100k 1=400k 2=1M (default 1)\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1<argc) sda = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1<argc) scl = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1<argc) speed = (uint8_t)atoi(argv[++i]);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x I2C scanner (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = sda; rel.PinIdList[1] = scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = sda; ov.PinIdScl = scl; ov.ClockSpeed = speed;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { printf("OpenI2C failed. Check pins/speed.\n"); return 3; }

    /* absent addresses never reply -> use a short per-probe timeout */
    rcp_set_timeout_ms(150);
    printf("\nScanning (SDA=PA%02u SCL=PA%02u) ...\n", sda, scl);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (i = 0; i < 0x80; ++i) {
        if ((i & 0x0F) == 0) printf("%02x:", i);
        if (i >= 0x08 && i <= 0x77) {
            WriteAndReadI2CVar_t wv; ReadI2CReply_t rr;
            memset(&wv, 0, sizeof(wv)); memset(&rr, 0, sizeof(rr));
            wv.HandleI2C = orep.HandleI2C; wv.DeviceAddress = (uint16_t)i;
            wv.ReadDataLength = 1; wv.WriteId = 0; wv.WriteDataLength = 0;
            if (rcp_write_and_read_i2c(&wv, &rr) == RT_OK) { printf(" %02x", i); found++; }
            else printf(" --");
        } else printf("   ");
        if ((i & 0x0F) == 0x0F) printf("\n");
    }
    printf("\n%d device(s) found on the I2C bus.\n", found);
    return 0;
}
