/*
 * ledblink.c  -  The "hello world" of this embedded toolset: blink the board's
 *                on-board LEDs over SOME/IP (RCP). Pure C.
 *
 * It drives the three on-board LEDs of the EVB-LAN8680-LAN866x as a running
 * light ("Lauflicht"): LD1 -> LD2 -> LD3 -> LD1 ..., each lit for half a second,
 * looping forever until Ctrl+C.
 *
 * The LEDs hang on the LAN8661 MCU GPIOs PA02 / PA06 / PA10 (SERCOM CS lines,
 * gated by DIP switch SW13-1/2/3). Those pins were found with `lan866x-ledscan`
 * (see led_map.json) and confirmed on the board.
 *
 * What this demonstrates: the full embedded-GPIO round trip done remotely -
 *   discover the endpoint (SOME/IP-SD) -> ReleaseDigitalPins -> OpenGpio (output)
 *   -> SetGpio high/low in a loop. The exact same rcp_* calls run unchanged on a
 *   32-bit MCU port (see PORTING.md).
 *
 * Usage:
 *   lan866x-ledblink                         running light on PA02,PA06,PA10
 *   lan866x-ledblink --ip 192.168.0.54
 *   lan866x-ledblink --pins 2,6,10           choose the LED pins
 *   lan866x-ledblink --beat 250              250 ms per step (default 500)
 *   lan866x-ledblink --all-on                static: turn all LEDs on, then exit
 */
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static const int DEFAULT_LEDS[] = { 2, 6, 10 };   /* LD1, LD2, LD3 (see led_map.json) */

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

/* Drive an already-opened GPIO to value (0/1). Returns 1 on success. */
static int set_pin(uint16_t handle, int value)
{
    SetGpioVar_t sv; memset(&sv, 0, sizeof(sv));
    sv.GpioValues[0] = (uint8_t)(handle >> 8);
    sv.GpioValues[1] = (uint8_t)(handle & 0xFF);
    sv.GpioValues[2] = (uint8_t)(value ? 1 : 0);
    sv.GpioValuesLength = 3;
    return rcp_set_gpio(&sv) == RT_OK;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, beat = 500, allOn = 0;
    int pins[16], nPins = 0;
    uint16_t handle[16];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ledblink - on-board LED running light over SOME/IP (pure C)\n"
                   "  --pins <list>   comma list of PA pins (default 2,6,10 = LD1,LD2,LD3)\n"
                   "  --beat <ms>     time per step (default 500)\n"
                   "  --all-on        turn all listed LEDs on once, then exit\n"
                   "  --ip/--ep       target endpoint\n"
                   "Runs forever; press Ctrl+C to stop (LEDs are turned off on exit).\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")   && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")     && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--beat")   && i+1<argc) beat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--all-on")) allOn = 1;
        else if (!strcmp(argv[i], "--pins") && i+1<argc) {
            char *tok = strtok(argv[++i], ",");
            while (tok && nPins < 16) {
                int p = atoi(tok);
                if (p >= 0 && p <= 15) pins[nPins++] = p;
                tok = strtok(NULL, ",");
            }
        }
    }
    if (nPins == 0) {
        nPins = (int)(sizeof(DEFAULT_LEDS) / sizeof(DEFAULT_LEDS[0]));
        for (i = 0; i < nPins; ++i) pins[i] = DEFAULT_LEDS[i];
    }
    if (beat < 1) beat = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x LED running-light demo (pure C)") < 0) return 2;

    /* Open every LED pin as a GPIO output (starts LOW = off). */
    for (i = 0; i < nPins; ++i) {
        ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
        memset(&rel, 0, sizeof(rel));
        rel.PinIdList[0] = (uint8_t)pins[i]; rel.PinIdListLength = 1;
        rcp_release_digital_pins(&rel);
        Sleep(20);                               /* pace control traffic (gotcha #4) */

        memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
        ov.PinIdGpio = (uint8_t)pins[i];
        ov.Direction = 1;                        /* output, LOW */
        if (rcp_open_gpio(&ov, &orep) != RT_OK) {
            printf("OpenGpio failed on PA%02d - aborting.\n", pins[i]);
            return 3;
        }
        handle[i] = orep.HandleGpio;
        Sleep(20);
    }

    if (allOn) {
        for (i = 0; i < nPins; ++i) set_pin(handle[i], 1);
        printf("All %d LED(s) ON. Exiting (handles stay open on the device).\n", nPins);
        return 0;
    }

    signal(SIGINT, on_sigint);
    printf("\nRunning light on");
    for (i = 0; i < nPins; ++i) printf(" PA%02d", pins[i]);
    printf("  (%d ms/step). Press Ctrl+C to stop.\n", beat);

    /* The running light: one LED lit at a time, advancing every <beat> ms. */
    while (g_run) {
        for (i = 0; i < nPins && g_run; ++i) {
            set_pin(handle[i], 1);               /* this LED on  */
            Sleep((DWORD)beat);
            set_pin(handle[i], 0);               /* this LED off */
        }
    }

    /* Clean exit: all LEDs off, release the pins. */
    for (i = 0; i < nPins; ++i) {
        ReleaseDigitalPinsVar_t rel;
        set_pin(handle[i], 0);
        memset(&rel, 0, sizeof(rel));
        rel.PinIdList[0] = (uint8_t)pins[i]; rel.PinIdListLength = 1;
        rcp_release_digital_pins(&rel);
        Sleep(20);
    }
    printf("\nStopped. LEDs off.\n");
    return 0;
}
