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
    int wantEp = 0, i, found = 0, probes = 1;
    uint8_t sda = 8, scl = 9, speed = 1;
    OpenI2CVar_t ov; OpenI2CReply_t orep; ReleaseDigitalPinsVar_t rel;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-i2cscan - scan an endpoint's I2C bus (pure C)\n"
                   "  --ip/--ep   target endpoint\n  --sda/--scl pins (default 8/9)\n"
                   "  --speed     0=100k 1=400k 2=1M (default 1)\n"
                   "  --probes N  confirm each address N times; report only if ALL ACK (default 1)\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sda")   && i+1<argc) sda = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scl")   && i+1<argc) scl = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1<argc) speed = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--probes")&& i+1<argc) probes = atoi(argv[++i]);
    }
    if (probes < 1) probes = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x I2C scanner (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = sda; rel.PinIdList[1] = scl; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdSda = sda; ov.PinIdScl = scl; ov.ClockSpeed = speed;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { printf("OpenI2C failed. Check pins/speed.\n"); return 3; }

    /* Probe with ReadI2C (method 0x1220) - the same call the original C++ tool
     * used. A present device ACKs -> RT_OK; absent -> NACK -> error. (The C port
     * had wrongly substituted WriteAndReadI2C with a 0-byte write, which this
     * firmware does not flag as NACK, giving false positives.) Retries cover a
     * host-dropped reply so the result reflects the real bus, not a lost packet. */
    rcp_set_timeout_ms(150);
    rcp_set_retries(3);
    printf("\nScanning (SDA=PA%02u SCL=PA%02u) ...\n", sda, scl);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    for (i = 0; i < 0x80; ++i) {
        if ((i & 0x0F) == 0) printf("%02x:", i);
        if (i >= 0x08 && i <= 0x77) {
            int p, ok = 1;
            for (p = 0; p < probes && ok; ++p) {
                ReadI2CVar_t rv; ReadI2CReply_t rr;
                memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr));
                rv.HandleI2C = orep.HandleI2C; rv.DeviceAddress = (uint16_t)i; rv.ReadDataLength = 1;
                if (rcp_read_i2c(&rv, &rr) != RT_OK) ok = 0;
                if (probes > 1) Sleep(8);
            }
            if (ok) { printf(" %02x", i); found++; }
            else printf(" --");
        } else printf("   ");
        if ((i & 0x0F) == 0x0F) printf("\n");
    }
    printf("\n%d device(s) found on the I2C bus.\n", found);
    return 0;
}
