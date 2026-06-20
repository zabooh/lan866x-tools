/*
 * ledscan.c  -  Interactive GPIO->LED mapper for a LAN866x endpoint. Pure C.
 *
 * Walks a list of candidate GPIO pins, blinks each one a few times over RCP
 * (OpenGpio/SetGpio), and asks the operator whether an on-board LED blinked.
 * The y/n answers are written to a JSON file so the result (which PA pin drives
 * which LED) can be read back and reused by other tools.
 *
 * On the EVB-LAN8680-LAN866x the 4 on-board LEDs (LD1..LD4) hang on MCU GPIOs
 * routed through SW13; the likely candidates are the CS / MOSI SERCOM lines:
 *   PA02 PA03 PA06 PA07 PA10 PA11 PA14 PA15  (this is the default set).
 *
 * Usage:
 *   lan866x-ledscan                       scan the default candidate pins
 *   lan866x-ledscan --ip 192.168.0.54
 *   lan866x-ledscan --pins 2,3,6,7        scan only these pins
 *   lan866x-ledscan --all                 scan PA00..PA15
 *   lan866x-ledscan --out my_leds.json    write results here (default led_map.json)
 *   lan866x-ledscan --blinks 8 --on 200 --off 200
 */
#include <stdlib.h>
#include <time.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

/* CS (P2) + MOSI (P3) SERCOM lines near SW13 - the likely LED candidates. */
static const int DEFAULT_CANDIDATES[] = { 2, 3, 6, 7, 10, 11, 14, 15 };

/* Drive PA<pin> as output and blink it. Returns RT_OK on success. The pin is
 * released first (per the other tools) and left driven LOW (LED off) at the end. */
static ReturnCode_t blink_pin(int pin, int blinks, int onMs, int offMs)
{
    ReleaseDigitalPinsVar_t rel; OpenGpioVar_t ov; OpenGpioReply_t orep;
    SetGpioVar_t sv; ReturnCode_t rc; int b;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[0] = (uint8_t)pin; rel.PinIdListLength = 1;
    rcp_release_digital_pins(&rel);
    Sleep(20);                                   /* pace control traffic (gotcha #4) */

    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdGpio = (uint8_t)pin;
    ov.Direction = 1;                            /* output, starts LOW */
    rc = rcp_open_gpio(&ov, &orep);
    if (rc != RT_OK) return rc;

    for (b = 0; b < blinks; ++b) {
        memset(&sv, 0, sizeof(sv));
        sv.GpioValues[0] = (uint8_t)(orep.HandleGpio >> 8);
        sv.GpioValues[1] = (uint8_t)(orep.HandleGpio & 0xFF);
        sv.GpioValues[2] = 1;                    /* HIGH */
        sv.GpioValuesLength = 3;
        if (rcp_set_gpio(&sv) != RT_OK) return RT_NOT_REACHABLE;
        Sleep(onMs);

        sv.GpioValues[2] = 0;                    /* LOW */
        if (rcp_set_gpio(&sv) != RT_OK) return RT_NOT_REACHABLE;
        Sleep(offMs);
    }
    return RT_OK;
}

/* Ask the operator a yes/no/repeat/quit question. Returns:
 *   1 = yes, 0 = no, 2 = repeat, -1 = quit/EOF  */
static int ask_user(int pin)
{
    char line[32];
    for (;;) {
        printf("    -> Did an LED blink for PA%02d ?  [y]es / [n]o / [r]epeat / [q]uit: ", pin);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) return -1;
        switch (line[0]) {
            case 'y': case 'Y': return 1;
            case 'n': case 'N': return 0;
            case 'r': case 'R': return 2;
            case 'q': case 'Q': return -1;
            default: printf("    please answer y, n, r or q.\n");
        }
    }
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL, *outPath = "led_map.json";
    int wantEp = 0, i, doAll = 0, blinks = 6, onMs = 250, offMs = 250;
    int pins[16], nPins = 0;
    int results[16];                             /* 1=led, 0=no, -1=skipped */
    char ipStr[20] = "?";
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    int sel;
    FILE *f;
    time_t now; struct tm *lt; char ts[32];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-ledscan - find which GPIO pin drives which on-board LED (pure C)\n"
                   "  --pins <list>   comma list of PA pins, e.g. 2,3,6,7 (default: candidate set)\n"
                   "  --all           scan PA00..PA15\n"
                   "  --out <file>    JSON output file (default led_map.json)\n"
                   "  --blinks <n>    blinks per pin (default 6)\n"
                   "  --on <ms>       LED-on time  (default 250)\n"
                   "  --off <ms>      LED-off time (default 250)\n"
                   "  --ip/--ep       target endpoint\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")     && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out")    && i+1<argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--blinks") && i+1<argc) blinks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--on")     && i+1<argc) onMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--off")    && i+1<argc) offMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--all")) doAll = 1;
        else if (!strcmp(argv[i], "--pins") && i+1<argc) {
            char *tok = strtok(argv[++i], ",");
            while (tok && nPins < 16) {
                int p = atoi(tok);
                if (p >= 0 && p <= 15) pins[nPins++] = p;
                tok = strtok(NULL, ",");
            }
        }
    }

    if (doAll) { for (i = 0; i < 16; ++i) pins[i] = i; nPins = 16; }
    else if (nPins == 0) {
        nPins = (int)(sizeof(DEFAULT_CANDIDATES) / sizeof(DEFAULT_CANDIDATES[0]));
        for (i = 0; i < nPins; ++i) pins[i] = DEFAULT_CANDIDATES[i];
    }
    if (blinks < 1) blinks = 1;

    if (tool_select(wantIp, wantEp, 5, "LAN866x LED scan tool (pure C)") < 0) return 2;

    /* remember the selected endpoint's IP for the JSON record */
    {
        uint8_t n = rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
        sel = tool_match_endpoint(wantIp, wantEp);
        if (sel >= 0 && sel < (int)n)
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                     eps[sel].ip[0], eps[sel].ip[1], eps[sel].ip[2], eps[sel].ip[3]);
    }

    printf("\nScanning %d candidate pin(s) on %s. Watch the board's LEDs.\n", nPins, ipStr);
    printf("Each pin blinks %dx (%d ms on / %d ms off), then you answer.\n\n", blinks, onMs, offMs);

    for (i = 0; i < nPins; ++i) {
        int ans;
        results[i] = -1;
        do {
            printf("[%d/%d] blinking PA%02d ...\n", i + 1, nPins, pins[i]);
            if (blink_pin(pins[i], blinks, onMs, offMs) != RT_OK) {
                printf("    OpenGpio/SetGpio failed on PA%02d - skipping.\n", pins[i]);
                ans = 0; results[i] = -1;        /* leave as skipped */
                break;
            }
            ans = ask_user(pins[i]);
            if (ans == 1)      results[i] = 1;
            else if (ans == 0) results[i] = 0;
            else if (ans == -1) { printf("\nAborted by user.\n"); nPins = i; goto write; }
            /* ans == 2 -> repeat the loop */
        } while (ans == 2);

        /* release the pin again before moving on */
        {
            ReleaseDigitalPinsVar_t rel; memset(&rel, 0, sizeof(rel));
            rel.PinIdList[0] = (uint8_t)pins[i]; rel.PinIdListLength = 1;
            rcp_release_digital_pins(&rel);
            Sleep(20);
        }
    }

write:
    now = time(NULL); lt = localtime(&now);
    if (lt) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt); else strcpy(ts, "?");

    f = fopen(outPath, "w");
    if (!f) { printf("ERROR: cannot write %s\n", outPath); return 5; }

    fprintf(f, "{\n");
    fprintf(f, "  \"tool\": \"lan866x-ledscan\",\n");
    fprintf(f, "  \"endpoint\": \"%s\",\n", ipStr);
    fprintf(f, "  \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "  \"blink\": { \"count\": %d, \"on_ms\": %d, \"off_ms\": %d },\n",
            blinks, onMs, offMs);
    fprintf(f, "  \"results\": [\n");
    for (i = 0; i < nPins; ++i) {
        const char *st = results[i] < 0 ? "skipped" : (results[i] == 1 ? "led" : "none");
        fprintf(f, "    { \"pin\": %d, \"name\": \"PA%02d\", \"led\": %s, \"status\": \"%s\" }%s\n",
                pins[i], pins[i], results[i] == 1 ? "true" : "false",
                st, i + 1 < nPins ? "," : "");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"leds_found\": [");
    {
        int first = 1;
        for (i = 0; i < nPins; ++i) if (results[i] == 1) {
            fprintf(f, "%s%d", first ? "" : ", ", pins[i]); first = 0;
        }
    }
    fprintf(f, "]\n");
    fprintf(f, "}\n");
    fclose(f);

    printf("\nResults written to %s\n", outPath);
    printf("LED pins found:");
    {
        int any = 0;
        for (i = 0; i < nPins; ++i) if (results[i] == 1) { printf(" PA%02d", pins[i]); any = 1; }
        if (!any) printf(" (none)");
    }
    printf("\n");
    return 0;
}
