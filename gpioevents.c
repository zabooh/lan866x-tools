/*
 * gpioevents.c  -  Live GPIO edge events from a LAN866x endpoint. Pure C.
 *
 * Didactic demo of the SOME/IP *event* path (device -> host notifications),
 * as opposed to polling. It:
 *   1. enables event subscription (rcp_enable_event_subscription) BEFORE init,
 *   2. opens a pin as input and arms it for edge capture (EnableGpioCaptureEvent),
 *   3. registers a callback (rcp_set_gpio_events_cb) and pumps rcp_poll(),
 *      printing each edge live as OnGpioEvents (0x8000) arrives.
 *
 * Drive the pin (button / wire to 3V3 or GND) and watch edges appear.
 *
 *   lan866x-gpioevents --pin 2                 both edges, run until Ctrl+C
 *   lan866x-gpioevents --pin 2 --edge rising --count 10
 *   lan866x-gpioevents --ip 192.168.0.54 --pin 3
 *
 * Read-only on the bus: it only opens an input and listens.
 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

static volatile int s_seen = 0;   /* events received so far */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint64_t be64(const uint8_t *p)
{
    uint64_t v = 0; int i;
    for (i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

/* OnGpioEvents.Events is a packed array of 13-byte records:
 *   uint16 Handle | uint8 State | uint8 Overflow | uint8 TimestampStatus | uint64 Timestamp
 * (big-endian on the wire). */
static void on_gpio_events(const OnGpioEventsNotification_t *ev, void *ctx)
{
    static const char *tss[] = { "unsynced", "uncertain", "certain", "invalid" };
    uint16_t i;
    (void)ctx;
    for (i = 0; i + 13 <= ev->EventsLength; i += 13) {
        const uint8_t *r = &ev->Events[i];
        uint16_t handle = be16(&r[0]);
        uint8_t  state  = r[2];
        uint8_t  ovf    = r[3];
        uint8_t  tstat  = r[4];
        uint64_t ts     = be64(&r[5]);
        printf("  [event %3d] handle=%u  state=%s  ts=%llu ns (%s)%s\n",
               ++s_seen, handle, state ? "HIGH" : "LOW", (unsigned long long)ts,
               tss[tstat & 3], ovf ? "  [OVERFLOW]" : "");
        fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, pin = -1, count = 0;
    uint8_t trigger = 2;   /* 0=falling, 1=rising, 2=both */
    ReleaseDigitalPinsVar_t rel;
    OpenGpioVar_t ov; OpenGpioReply_t orep;
    EnableGpioCaptureEventVar_t ce;
    DisableGpioEventVar_t de;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-gpioevents - live GPIO edge events (SOME/IP notifications)\n\n"
                   "  --pin <0..15>      pin PA00..PA15 to watch (required)\n"
                   "  --edge <both|rising|falling>   edge to capture (default both)\n"
                   "  --count <n>        stop after n events (default 0 = run forever)\n"
                   "  --ip <addr> | --ep <i>          target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")    && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pin")   && i+1<argc) pin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i+1<argc) count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--edge")  && i+1<argc) {
            const char *e = argv[++i];
            trigger = !strcmp(e, "rising") ? 1u : !strcmp(e, "falling") ? 0u : 2u;
        }
    }
    if (pin < 0) { printf("Need --pin <0..15>. Use --help.\n"); return 1; }

    /* MUST enable subscription BEFORE rcp_init() (tool_select calls it). */
    rcp_enable_event_subscription(true);
    rcp_set_gpio_events_cb(on_gpio_events, NULL);

    if (tool_select(wantIp, wantEp, 5, "LAN866x live GPIO events (pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1; rcp_release_digital_pins(&rel);

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = (uint8_t)pin; ov.Direction = 0 /* input */;
    if (rcp_open_gpio(&ov, &orep) != RT_OK) { printf("OpenGpio failed.\n"); return 3; }

    memset(&ce, 0, sizeof(ce));
    ce.HandleGpio = orep.HandleGpio;
    ce.NotificationType = 2;   /* Data */
    ce.Trigger = trigger;
    ce.Timestamped = 1;
    {
        ReturnCode_t erc = rcp_enable_gpio_capture_event(&ce);
        if (erc != RT_OK) {
            printf("EnableGpioCaptureEvent failed (rc=%d)%s.\n", erc,
                   erc == RT_UNKNOWN_METHOD ? " - not in this firmware build" :
                   " - firmware/config may not support it");
            return 4;
        }
    }

    printf("\nWatching PA%02d for %s edge(s). Drive the pin to see events.%s\n\n",
           pin, trigger == 2 ? "both" : trigger == 1 ? "rising" : "falling",
           count ? "" : " Ctrl+C to stop.");

    while (count == 0 || s_seen < count) {
        rcp_poll();          /* delivers OnGpioEvents to on_gpio_events() */
        Sleep(5);
    }

    memset(&de, 0, sizeof(de)); de.HandleGpio = orep.HandleGpio;
    rcp_disable_gpio_event(&de);
    printf("\nDone (%d events).\n", s_seen);
    return 0;
}
