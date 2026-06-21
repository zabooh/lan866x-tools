/*
 * adc.c  -  Read the on-chip ADC of a LAN866x endpoint. Pure C.
 *
 * Usage:
 *   lan866x-adc                 single analog read, 3V3 reference
 *   lan866x-adc --temp          internal temperature sensor
 *   lan866x-adc --vref 1        1V1 reference
 *   lan866x-adc --count 10 --interval 200
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, count = 1, interval = 200, n;
    uint8_t channel = 0, vref = 0;
    OpenAdcVar_t ov; OpenAdcReply_t orep; CloseAdcVar_t cv;
    double vfull;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-adc - read the on-chip ADC (pure C)\n"
                   "  --channel 0|1  0=analog input (default), 1=internal temperature\n"
                   "  --temp         shortcut for --channel 1\n"
                   "  --vref 0|1     0=3V3 (default), 1=1V1\n"
                   "  --count N      reads (default 1; 0=endless)   --interval ms\n"
                   "  --ip/--ep      target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")       && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")       && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--channel")  && i+1<argc) channel = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--temp"))                 channel = 1;
        else if (!strcmp(argv[i], "--vref")     && i+1<argc) vref = (uint8_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count")    && i+1<argc) count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--interval") && i+1<argc) interval = atoi(argv[++i]);
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x ADC tool (pure C)") < 0) return 2;

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep)); ov.PinId = 0;
    if (rcp_open_adc(&ov, &orep) != RT_OK) { printf("OpenAdc failed (ADC not configured?).\n"); return 3; }

    vfull = (vref == 1) ? 1.1 : 3.3;
    printf("\nADC open: %s, reference %s\n",
           channel == 1 ? "internal temperature" : "analog input", vref == 1 ? "1V1" : "3V3");

    for (n = 0; count == 0 || n < count; ++n) {
        ReadAdcVar_t rv; ReadAdcReply_t rr;
        memset(&rv, 0, sizeof(rv)); memset(&rr, 0, sizeof(rr));
        rv.HandleAdc = orep.HandleAdc; rv.ChannelSelect = channel; rv.VoltageReference = vref;
        if (rcp_read_adc(&rv, &rr) == RT_OK) {
            if (channel == 1) printf("  raw=%4u  (internal temperature sensor)\n", rr.ReadData);
            else printf("  raw=%4u  =  %.3f V\n", rr.ReadData, (double)rr.ReadData / 4095.0 * vfull);
        } else { printf("ReadAdc failed.\n"); break; }
        if (count == 0 || n + 1 < count) Sleep(interval);
    }

    memset(&cv, 0, sizeof(cv)); cv.HandleAdc = orep.HandleAdc; rcp_close_adc(&cv);
    return 0;
}
