/*
 * gpio.c  -  Set or read a GPIO pin on a LAN866x endpoint. Pure C.
 *
 * Usage:
 *   lan866x-gpio --pin 2 --set 1     PA02 output high
 *   lan866x-gpio --pin 2 --get       PA02 input, read
 *   lan866x-gpio --ip 192.168.0.54 --pin 6 --set 0
 */
#include <stdlib.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, pin = -1, setVal = -1, doGet = 0;
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-gpio - set/read a GPIO pin (pure C)\n"
                   "  --pin <0..15>   pin PA00..PA15 (required)\n"
                   "  --set <0|1>     drive output low/high\n"
                   "  --get           read as input\n  --ip/--ep  target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")  && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin") && i+1<argc) pin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--set") && i+1<argc) setVal = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--get")) doGet = 1;
    }
    if (pin < 0 || (setVal < 0 && !doGet)) {
        printf("Need --pin and (--set <0|1> or --get). Use --help.\n"); return 1;
    }

    if (tool_select(wantIp, wantEp, 5, "LAN866x GPIO tool (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1; rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = (uint8_t)pin;
    ov.Direction = doGet ? 0 /* input */ : 1 /* output low */;
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { printf("OpenGpio failed.\n"); return 3; }

    if (doGet) {
        GetGpioReply_t gr; memset(&gr, 0, sizeof(gr));
        if (rcp_get_gpio(&gr) != RT_OK) { printf("GetGpio failed.\n"); return 4; }
        /* GpioValues = 3-byte tuples [handleHi, handleLo, value] */
        int v = -1;
        for (i = 0; i + 2 < (int)gr.GpioValuesLength; i += 3) {
            uint16_t h = (uint16_t)((gr.GpioValues[i] << 8) | gr.GpioValues[i+1]);
            if (h == orep.HandleGpio) v = gr.GpioValues[i+2];
        }
        printf("PA%02d = %s\n", pin, v < 0 ? "?" : (v ? "HIGH (1)" : "LOW (0)"));
    } else {
        SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
        sv.GpioValues[0] = (uint8_t)(orep.HandleGpio >> 8);
        sv.GpioValues[1] = (uint8_t)(orep.HandleGpio & 0xFF);
        sv.GpioValues[2] = (uint8_t)(setVal ? 1 : 0);
        sv.GpioValuesLength = 3;
        if (rcp_set_gpio(&sv) != RT_OK) { printf("SetGpio failed.\n"); return 4; }
        printf("PA%02d driven %s\n", pin, setVal ? "HIGH (1)" : "LOW (0)");
    }
    return 0;
}
