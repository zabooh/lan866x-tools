/*
 * thumbmon.c  -  Live Thumbstick monitor over SPI, non-blocking, via SOME/IP (RCP).
 *                Pure C.
 *
 * The sequel to lan866x-spiid: it reads the MCP3204 X/Y axes continuously and
 * shows a live position read-out. One async SPI transfer is in flight at a time
 * (alternating X/Y), and the render loop runs at a steady cadence regardless of
 * reply timing - the "poll a sensor in a superloop without stalling" pattern.
 *
 * Wire: MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15, mode 1, ~1.92 MHz (Click slot 4).
 *
 * Usage:
 *   lan866x-thumbmon --ip 192.168.0.54
 *   lan866x-thumbmon --hz 30
 */
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

#define ADC_MAX 0x0FFFu

static volatile sig_atomic_t g_run = 1;
static void on_sigint(int sig) { (void)sig; g_run = 0; }

static uint16_t s_handle;
static uint32_t s_wid = 0;
static volatile int      s_pending = 0;
static volatile int      s_curCh = 1;            /* channel of the in-flight read */
static volatile uint16_t s_x = ADC_MAX / 2, s_y = ADC_MAX / 2;

/* async reply: decode the 12-bit value, store into the axis we asked for */
static void on_spi(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_spi1(rx, rxLen, d, &l) && l >= 3) {
            uint16_t v = (uint16_t)(((d[1] << 8) | d[2]) & ADC_MAX);
            if (s_curCh == 1) s_x = v; else s_y = v;
        }
    }
    s_pending = 0;
}

/* map a 12-bit axis value to a 0..w-1 marker column */
static int axis_col(uint16_t v, int w) { int c = (ADC_MAX - v) * (w - 1) / ADC_MAX; return c < 0 ? 0 : (c >= w ? w - 1 : c); }

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    int wantEp = 0, i, miso = 12, sck = 13, cs = 14, mosi = 15, mode = 1, hz = 20, p = 0;
    long speed = 1923000;
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    DWORD nextRender;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-thumbmon - live MCP3204 thumbstick read over SPI, non-blocking (pure C)\n"
                   "  --hz <n>    render rate (default 20)\n"
                   "  --miso/--sck/--cs/--mosi <n>   SPI pins (default 12/13/14/15)\n"
                   "  --mode <0..3> (default 1)   --speed <hz> (default 1923000)\n"
                   "  --ip/--ep   target endpoint\n"
                   "Ctrl+C to stop.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")  && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")    && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hz")    && i+1<argc) hz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--miso")  && i+1<argc) miso = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sck")   && i+1<argc) sck  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cs")    && i+1<argc) cs   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mosi")  && i+1<argc) mosi = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode")  && i+1<argc) mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i+1<argc) speed = strtol(argv[++i], NULL, 0);
    }
    if (hz < 1) hz = 1; if (hz > 100) hz = 100;

    if (tool_select(wantIp, wantEp, 5, "LAN866x thumbstick monitor (non-blocking, pure C)") < 0) return 2;

    memset(&rel, 0, sizeof(rel));
    rel.PinIdList[p++] = (uint8_t)miso; rel.PinIdList[p++] = (uint8_t)sck;
    rel.PinIdList[p++] = (uint8_t)cs;   rel.PinIdList[p++] = (uint8_t)mosi;
    rel.PinIdListLength = (uint8_t)p;
    rcp_release_digital_pins(&rel); Sleep(25);
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    ov.PinIdMiso = (uint8_t)miso; ov.PinIdSck = (uint8_t)sck; ov.PinIdCs = (uint8_t)cs;
    ov.PinIdMosi = (uint8_t)mosi; ov.Mode = (uint8_t)mode; ov.ClockSpeed = (uint32_t)speed;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { printf("OpenSpi failed.\n"); return 3; }
    s_handle = orep.HandleSpi;

    rcp_set_async_timeout_ms(150);
    signal(SIGINT, on_sigint);
    printf("\nLive thumbstick (MCP3204). Move the stick. Ctrl+C to stop.\n\n");

    nextRender = GetTickCount();
    while (g_run) {
        /* one read in flight at a time, alternating X (ch1) and Y (ch0) */
        if (!s_pending) {
            int ch = (s_curCh == 1) ? 0 : 1;       /* alternate */
            uint8_t tx[3], params[64]; uint16_t n;
            tx[0] = 0x06; tx[1] = (uint8_t)(ch << 6); tx[2] = 0xFF;
            n = rcp_enc_spi1(params, sizeof(params), s_handle, s_wid++, tx, 3, 3);
            if (n && rcp_async_request(0x1508u, params, n, on_spi, NULL) == RT_OK) { s_curCh = ch; s_pending = 1; }
        }
        rcp_async_poll();

        {
            DWORD now = GetTickCount();
            if ((long)(now - nextRender) >= 0) {
                int W = 21, cx = axis_col(s_x, W), cy, k;
                printf("\r  X[");
                for (k = 0; k < W; ++k) putchar(k == cx ? 'o' : '-');
                cy = (ADC_MAX - s_y) * 100 / ADC_MAX;
                printf("] x=%4u y=%4u (y=%3d%%)  ", s_x, s_y, cy);
                fflush(stdout);
                nextRender += (DWORD)(1000 / hz);
            }
        }
        Sleep(2);
    }

    { CloseSpiVar_t c; c.HandleSpi = s_handle; rcp_close_spi(&c); }
    printf("\nStopped.\n");
    return 0;
}
