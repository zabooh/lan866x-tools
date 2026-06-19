/*
 * clickdemo.c  -  Interactive MikroE Click demo for a LAN866x lighting endpoint.
 *                 Pure C, Windows.
 *
 * Two 10x10 RGB Click displays (WS2812) are driven from two sensors:
 *   - RGB Click in slot 1 (left half)  <- Thumbstick Click  (SPI, slot 4)
 *   - RGB Click in slot 2 (right half) <- Proximity 3 Click (I2C, slot 3)
 *
 * Sensors are read over the RCP service (OpenSpi/WriteAndReadSpi for the
 * MCP3204 in the Thumbstick; OpenI2C/WriteI2C/WriteAndReadI2C for the VCNL4200
 * in the Proximity 3). The displays are not directly addressable - the lighting
 * firmware renders them from an RTP/RFC4175 video stream on UDP port 5001, so we
 * build a 20x10 RGB frame (left 10 columns = display 1, right 10 = display 2)
 * and send it each loop. This mirrors the official "gameloop-example".
 *
 *   lan866x-clickdemo --ip 192.168.0.54
 *
 * Pin map (matches the demo config / board DIP defaults):
 *   Thumbstick SPI : MISO=PA12 SCK=PA13 CS=PA14 MOSI=PA15  (mode 1, 1.923 MHz)
 *   Proximity  I2C : SDA=PA08 SCL=PA09  (400 kHz), VCNL4200 @ 0x51
 *
 * Ctrl-C to stop (clears both displays first).
 */

#include <winsock2.h>     /* before any windows.h pulled in by tool_common.h */
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "rcp.h"
#include "tool_common.h"
#include <mmsystem.h>     /* timeBeginPeriod: 1 ms scheduler tick for snappy I/O */

uint8_t MULTICAST_IP[] = { 224, 0, 0, 1 };

/* --- frame / RTP -------------------------------------------------------- */
#define X_RES       20
#define Y_RES       10
#define RTP_PORT    5001
#define RTP_TYPE    96            /* RFC4175 dynamic payload type (default 96) */
#define ADC_MAX     0x0FFF        /* MCP3204 is 12-bit */

/* --- pins (PA index == pin id) ------------------------------------------ */
#define SPI_MISO 12u
#define SPI_SCK  13u
#define SPI_CS   14u
#define SPI_MOSI 15u
#define I2C_SDA   8u
#define I2C_SCL   9u

#define VCNL4200_ADDR        0x51u
#define VCNL4200_ID_VALUE    0x1058u
#define VCNL4200_PS_CONF1    0x03u
#define VCNL4200_PS_CONF3    0x04u
#define VCNL4200_ALS_CONF    0x00u
#define VCNL4200_PS_DATA     0x08u
#define VCNL4200_ID_REG      0x0Eu

static uint8_t  s_fb[Y_RES][X_RES][3];
static SOCKET   s_rtp = INVALID_SOCKET;
static uint8_t  s_ip[4];
static uint32_t s_seq = 0, s_ssrc = 0;
static volatile int s_run = 1;

static void on_sigint(int sig) { (void)sig; s_run = 0; }

/* --- event log ----------------------------------------------------------- *
 * One CSV row per event, with the UTC epoch (== tshark frame.time_epoch, so it
 * lines up 1:1 with the Wireshark capture) and the SOME/IP session id (==
 * someip.sessionid) so each request/reply can be matched to its packet. The
 * "why" falls straight out: a THUMB_TMO/PROX_TMO row whose sid IS present as a
 * response on the wire means the Windows host dropped that reply (gotcha #4).
 * Columns: epoch,rel_ms,event,sid,v1,v2,rc,lat_ms                            */
static FILE *s_log = NULL;
static LARGE_INTEGER s_qpcFreq, s_qpcStart;

static double log_epoch(void)   /* UTC seconds since 1970 (~sub-ms resolution) */
{
    FILETIME ft; ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1.0e7;
}
static double log_relms(void)   /* monotonic ms since start */
{
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)(c.QuadPart - s_qpcStart.QuadPart) * 1000.0 / (double)s_qpcFreq.QuadPart;
}
static void elog(const char *ev, unsigned sid, int v1, int v2, int rc, double lat)
{
    char line[160]; int n;
    if (!s_log) return;
    n = snprintf(line, sizeof(line), "%.6f,%.3f,%s,%u,%d,%d,%d,%.3f\n",
                 log_epoch(), log_relms(), ev, sid, v1, v2, rc, lat);
    if (n > 0) fputs(line, s_log);   /* single write = atomic under the CRT FILE lock */
}

static uint32_t now_us10(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint32_t)(((c.QuadPart * 1000000ULL) / (uint64_t)f.QuadPart) / 10ULL);
}

/* --- RTP/RFC4175: send columns [x0 .. x0+w) of the frame in one packet ----
 * w==X_RES sends the whole 20x10 frame; w==10 with x0 0/10 sends just the
 * left/right 10x10 display (RFC4175 carries the pixel offset within the line,
 * so the firmware updates only those columns). */
static void rtp_send_region(int x0, int w)
{
    uint8_t pkt[64 + Y_RES * 6 + Y_RES * X_RES * 3];
    int n = 0, x, y;
    uint32_t ts = now_us10();
    struct sockaddr_in dst;

    pkt[n++] = 0x80;                                  /* V=2, no pad/ext/cc   */
    pkt[n++] = (uint8_t)(0x80u | (RTP_TYPE & 0x7Fu)); /* marker + payload type*/
    pkt[n++] = (uint8_t)(s_seq >> 8);  pkt[n++] = (uint8_t)s_seq;
    pkt[n++] = (uint8_t)(ts >> 24); pkt[n++] = (uint8_t)(ts >> 16);
    pkt[n++] = (uint8_t)(ts >> 8);  pkt[n++] = (uint8_t)ts;
    pkt[n++] = (uint8_t)(s_ssrc >> 24); pkt[n++] = (uint8_t)(s_ssrc >> 16);
    pkt[n++] = (uint8_t)(s_ssrc >> 8);  pkt[n++] = (uint8_t)s_ssrc;
    pkt[n++] = (uint8_t)(s_seq >> 24); pkt[n++] = (uint8_t)(s_seq >> 16); /* RFC4175 ext seq */

    for (y = 0; y < Y_RES; ++y) {                     /* one line header per scan line */
        int cont = (y != (Y_RES - 1));
        uint16_t len = (uint16_t)(w * 3);
        pkt[n++] = (uint8_t)(len >> 8);  pkt[n++] = (uint8_t)len;
        pkt[n++] = (uint8_t)(y >> 8);    pkt[n++] = (uint8_t)y;
        pkt[n++] = (uint8_t)((cont ? 0x80 : 0x00) | ((x0 >> 8) & 0x7F)); /* offset = x0 */
        pkt[n++] = (uint8_t)x0;
    }
    for (y = 0; y < Y_RES; ++y)
        for (x = x0; x < x0 + w; ++x) {
            pkt[n++] = s_fb[y][x][0]; pkt[n++] = s_fb[y][x][1]; pkt[n++] = s_fb[y][x][2];
        }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(RTP_PORT);
    dst.sin_addr.s_addr = (uint32_t)s_ip[0] | ((uint32_t)s_ip[1] << 8) |
                          ((uint32_t)s_ip[2] << 16) | ((uint32_t)s_ip[3] << 24);
    sendto(s_rtp, (const char *)pkt, n, 0, (struct sockaddr *)&dst, sizeof(dst));
    s_seq++;
}
static void rtp_send(void) { rtp_send_region(0, X_RES); }   /* full frame (clear on exit) */

/* --- Thumbstick (MCP3204 over SPI) -------------------------------------- */
static uint16_t s_spi = UINT16_MAX;
static uint32_t s_spiWid = 0;

static int spi_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenSpiVar_t ov; OpenSpiReply_t orep;
    uint16_t p = 0;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[p++] = SPI_MISO; rel.PinIdList[p++] = SPI_SCK;
    rel.PinIdList[p++] = SPI_CS;   rel.PinIdList[p++] = SPI_MOSI;
    rel.PinIdListLength = p; rcp_release_digital_pins(&rel);
    Sleep(25);   /* space requests: back-to-back, the host drops the 2nd reply */
    ov.PinIdMiso = SPI_MISO; ov.PinIdSck = SPI_SCK; ov.PinIdCs = SPI_CS;
    ov.PinIdMosi = SPI_MOSI; ov.Mode = 1;
    ov.ClockSpeed = 1923000u;  /* compound works at any supported clock */
    s_spiWid = 0;
    if (rcp_open_spi(&ov, &orep) != RT_OK) { s_spi = UINT16_MAX; return 0; }
    s_spi = orep.HandleSpi;
    return 1;
}

/* --- Proximity 3 (VCNL4200 over I2C) ------------------------------------ */
static uint16_t s_i2c = UINT16_MAX;
static uint32_t s_i2cWid = 0;
static int      s_proxInit = 0;

static int i2c_setup(void)
{
    ReleaseDigitalPinsVar_t rel; OpenI2CVar_t ov; OpenI2CReply_t orep;
    memset(&ov, 0, sizeof(ov)); memset(&orep, 0, sizeof(orep));
    rel.PinIdList[0] = I2C_SDA; rel.PinIdList[1] = I2C_SCL; rel.PinIdListLength = 2;
    rcp_release_digital_pins(&rel);
    Sleep(25);   /* space requests: back-to-back, the host drops the 2nd reply */
    ov.PinIdSda = I2C_SDA; ov.PinIdScl = I2C_SCL; ov.ClockSpeed = 1; /* 400 kHz */
    s_i2cWid = 0; s_proxInit = 0;
    if (rcp_open_i2c(&ov, &orep) != RT_OK) { s_i2c = UINT16_MAX; return 0; }
    s_i2c = orep.HandleI2C;
    return 1;
}

static int i2c_write(const uint8_t *tx, uint16_t n)
{
    WriteI2CVar_t w; memset(&w, 0, sizeof(w));
    w.HandleI2C = s_i2c; w.DeviceAddress = VCNL4200_ADDR; w.WriteId = s_i2cWid++;
    w.WriteDataLength = n; memcpy(w.WriteData, tx, n);
    return rcp_write_i2c(&w) == RT_OK;
}

static int i2c_read_reg(uint8_t reg, uint16_t *val)
{
    WriteAndReadI2CVar_t rq; ReadI2CReply_t rp;
    memset(&rq, 0, sizeof(rq)); memset(&rp, 0, sizeof(rp));
    rq.HandleI2C = s_i2c; rq.DeviceAddress = VCNL4200_ADDR; rq.ReadDataLength = 2;
    rq.WriteId = s_i2cWid++; rq.WriteDataLength = 1; rq.WriteData[0] = reg;
    if (rcp_write_and_read_i2c(&rq, &rp) != RT_OK || rp.ReadDataLength < 2) return 0;
    *val = (uint16_t)(rp.ReadData[0] | (rp.ReadData[1] << 8)); /* VCNL4200 = LSB first */
    return 1;
}

static int vcnl4200_init(void)
{
    uint16_t id = 0;
    uint8_t b[3];
    if (!i2c_read_reg(VCNL4200_ID_REG, &id) || id != VCNL4200_ID_VALUE) return 0;
    /* PS_CONF1/2: fast response. IT 2T + duty 1/160 (highest) raise the
     * measurement rate a lot vs the demo's IT 9T + 1/320 (which favours range
     * but updates only a few times/s). AF enable kept as in the demo. */
    /* Fastest practical PS: duty 1/160 (highest = fastest response per the
     * datasheet), 1 multi-pulse (default, fastest), IT 1.5T (short = fast),
     * 16-bit output. Active-force stays OFF (continuous measurement). */
    b[0] = VCNL4200_PS_CONF1; b[1] = 0x02 | 0x00; b[2] = 0x08;  /* IT_1.5T | DUTY_1_160 ; PS_HD 16-bit */
    Sleep(25); if (!i2c_write(b, 3)) return 0;
    /* PS_CONF3/MS: sunlight cancel enable ; LED 200 mA (max IR, keeps signal up at short IT) */
    b[0] = VCNL4200_PS_CONF3; b[1] = 0x01; b[2] = 0x07;  /* LED_I_200mA */
    Sleep(25); if (!i2c_write(b, 3)) return 0;
    /* ALS_CONF: enabled */
    b[0] = VCNL4200_ALS_CONF; b[1] = 0x00; b[2] = 0x00;
    Sleep(25); if (!i2c_write(b, 3)) return 0;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Proximity as a 1-pixel-high dim-blue bar on the right display (cols 10-19)
 * whose row tracks distance, mapping raw [2 .. full] over the full height:
 * raw 2 = bottom row, raw=full = top row. raw 0 (no/invalid reading) leaves the
 * bar where it was (no change). */
/* Thumbstick as an orange "flashlight" spot on the left display (cols 0-9):
 * centred at rest, reaching the edge at full deflection, dark background. The
 * glow is a soft parabolic falloff around the mapped centre (no libm needed). */
static void thumb_spot(uint16_t xr, uint16_t yr, int bright)
{
    int x, y;
    double cx = (double)(ADC_MAX - xr) * 9.0 / (double)ADC_MAX;  /* both axes inverted to match the */
    double cy = (double)(ADC_MAX - yr) * 9.0 / (double)ADC_MAX;  /* stick; centre (~2048) -> ~4.5    */
    const double r2 = 6.25;                            /* spot radius^2 (~2.5 px) */
    for (y = 0; y < Y_RES; ++y)
        for (x = 0; x < 10; ++x) {
            double dx = (double)x - cx, dy = (double)y - cy;
            double in = 1.0 - (dx * dx + dy * dy) / r2;
            if (in < 0.0) in = 0.0;
            s_fb[y][x][0] = (uint8_t)((double)bright * in);          /* orange: R   */
            s_fb[y][x][1] = (uint8_t)((double)bright * in * 0.55);   /* orange: G   */
            s_fb[y][x][2] = 0;
        }
}

static void prox_bar(uint16_t raw, int full, int blue)
{
    static int row = Y_RES - 1;       /* persists across calls; raw==0 keeps it */
    const int lo = 2;
    int x, y;
    if (full <= lo) full = lo + 1;
    if (raw != 0u) {
        int v = (int)raw;
        if (v < lo) v = lo; else if (v > full) v = full;
        row = (Y_RES - 1) * (full - v) / (full - lo);   /* v=lo -> bottom, v=full -> top */
    }
    for (y = 0; y < Y_RES; ++y)
        for (x = 10; x < X_RES; ++x) {
            s_fb[y][x][0] = 0; s_fb[y][x][1] = 0;
            s_fb[y][x][2] = (y == row) ? (uint8_t)blue : 0u;
        }
}

/* --- async sensor state (written by callbacks, read by the render loop) -- */
static volatile uint16_t s_tx = ADC_MAX / 2, s_ty = ADC_MAX / 2, s_prox = 0;
static volatile int s_thumbPending = 0, s_proxPending = 0;
static volatile int s_thumbOk = 0, s_proxOk = 0;
/* per-sensor request bookkeeping for the event log (one in flight at a time) */
static volatile unsigned s_thumbSid = 0, s_proxSid = 0;
static volatile double   s_thumbReqMs = 0.0, s_proxReqMs = 0.0;

/* fires on the rx thread (response) or from rcp_async_poll (timeout) */
static void on_thumb(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t r0[3] = {0}, r1[3] = {0}; uint16_t l0 = 3, l1 = 3;
        if (rcp_dec_spi2(rx, rxLen, r0, &l0, r1, &l1) && l0 >= 3 && l1 >= 3) {
            s_ty = (uint16_t)(((r0[1] << 8) | r0[2]) & 0x0FFF);   /* ch0 = Y */
            s_tx = (uint16_t)(((r1[1] << 8) | r1[2]) & 0x0FFF);   /* ch1 = X */
            s_thumbOk = 1;
            elog("THUMB_RSP", s_thumbSid, s_tx, s_ty, rc, log_relms() - s_thumbReqMs);
        } else { s_thumbOk = 0; elog("THUMB_BAD", s_thumbSid, 0, 0, rc, log_relms() - s_thumbReqMs); }
    } else { s_thumbOk = 0; elog("THUMB_TMO", s_thumbSid, 0, 0, rc, log_relms() - s_thumbReqMs); }
    s_thumbPending = 0;
}
static void on_prox(void *ctx, ReturnCode_t rc, const uint8_t *rx, uint16_t rxLen)
{
    (void)ctx;
    if (rc == RT_OK && rx) {
        uint8_t d[8] = {0}; uint16_t l = sizeof(d);
        if (rcp_dec_i2c_read(rx, rxLen, d, &l) && l >= 2) {
            s_prox = (uint16_t)(d[0] | (d[1] << 8));             /* VCNL4200 LSB first */
            s_proxOk = 1;
            elog("PROX_RSP", s_proxSid, s_prox, 0, rc, log_relms() - s_proxReqMs);
        } else { s_proxOk = 0; elog("PROX_BAD", s_proxSid, 0, 0, rc, log_relms() - s_proxReqMs); }
    } else { s_proxOk = 0; elog("PROX_TMO", s_proxSid, 0, 0, rc, log_relms() - s_proxReqMs); }
    s_proxPending = 0;
}

/* Fire one async sensor read. Returns 1 if a request actually went out, 0 if it
 * was skipped (peripheral unavailable, or its previous reply is still pending).
 * Exactly one of these is called per loop tick, so reads go out solo (the host
 * drops one of two replies that arrive back-to-back). */
static int fire_thumb(void)
{
    uint8_t params[64], c0[3] = { 0x06, 0x00, 0xFF }, c1[3] = { 0x06, 0x40, 0xFF };
    uint16_t pl;
    if (s_spi == UINT16_MAX || s_thumbPending) return 0;
    pl = rcp_enc_spi2(params, sizeof(params), s_spi, s_spiWid++, c0, 3, c1, 3, 3, 3);
    s_thumbReqMs = log_relms(); s_thumbPending = 1;
    if (pl && rcp_async_request(0x1509u, params, pl, on_thumb, NULL) == RT_OK) {
        s_thumbSid = rcp_async_last_sid();
        elog("THUMB_REQ", s_thumbSid, 0, 0, 0, 0.0);
        return 1;
    }
    s_thumbPending = 0; return 0;
}
static int fire_prox(void)
{
    uint8_t params[64], reg = VCNL4200_PS_DATA;
    uint16_t pl;
    if (!s_proxInit || s_proxPending) return 0;
    pl = rcp_enc_i2c_read(params, sizeof(params), s_i2c, VCNL4200_ADDR, s_i2cWid++, &reg, 1, 2);
    s_proxReqMs = log_relms(); s_proxPending = 1;
    if (pl && rcp_async_request(0x1208u, params, pl, on_prox, NULL) == RT_OK) {
        s_proxSid = rcp_async_last_sid();
        elog("PROX_REQ", s_proxSid, 0, 0, 0, 0.0);
        return 1;
    }
    s_proxPending = 0; return 0;
}

int main(int argc, char **argv)
{
    const char *wantIp = NULL;
    const char *logPath = "clickdemo-events.csv";
    int wantEp = 0, i, fps = 50, sel, bright = 128, proxMax = 400, barBlue = 64;
    rcp_endpoint_t eps[RCP_MAX_ENDPOINTS];
    WSADATA wsa;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("lan866x-clickdemo - drive two RGB Click displays from Thumbstick + Proximity\n\n"
                   "  lan866x-clickdemo [--ip <addr>|--ep <i>] [--fps N] [--bright 0..255]\n"
                   "                    [--prox-max N] [--bar 0..255] [--log <file>|--nolog]\n\n"
                   "  Left display  (slot 1): orange spot steered by the Thumbstick (slot 4, SPI).\n"
                   "  Right display (slot 2) shows a 1-px blue Proximity bar (slot 3, I2C): raw 2=bottom..max=top.\n"
                   "  --prox-max: proximity raw value that puts the bar at the top (default 400).\n"
                   "  --bar: blue brightness of the proximity bar, 0..255 (default 64).\n"
                   "  --log <file>: event log for analysis (default clickdemo-events.csv); --nolog to disable.\n");
            return 0;
        } else if (!strcmp(argv[i], "--ip")       && i+1<argc) wantIp = argv[++i];
        else if (!strcmp(argv[i], "--ep")       && i+1<argc) wantEp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fps")      && i+1<argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bright")   && i+1<argc) bright = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prox-max") && i+1<argc) proxMax = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bar")      && i+1<argc) barBlue = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--log")      && i+1<argc) logPath = argv[++i];
        else if (!strcmp(argv[i], "--nolog")) logPath = NULL;
    }
    if (fps < 1) fps = 1; if (fps > 200) fps = 200;
    if (bright < 1) bright = 1; if (bright > 255) bright = 255;
    if (proxMax < 1) proxMax = 1;
    if (barBlue < 0) barBlue = 0; if (barBlue > 255) barBlue = 255;

    sel = tool_select(wantIp, wantEp, 5, "LAN866x Click demo (Thumbstick + Proximity -> RGB)");
    if (sel < 0) return 2;
    rcp_get_endpoints(eps, RCP_MAX_ENDPOINTS);
    memcpy(s_ip, eps[sel].ip, 4);
    printf("Target %u.%u.%u.%u, streaming RTP video to :%d at %d fps. Ctrl-C to stop.\n",
           s_ip[0], s_ip[1], s_ip[2], s_ip[3], RTP_PORT, fps);

    /* event log: epoch lines up with the Wireshark capture (frame.time_epoch) */
    QueryPerformanceFrequency(&s_qpcFreq);
    QueryPerformanceCounter(&s_qpcStart);
    if (logPath) {
        s_log = fopen(logPath, "w");
        if (s_log) {
            fputs("epoch,rel_ms,event,sid,v1,v2,rc,lat_ms\n", s_log);
            printf("  event log -> %s\n", logPath);
        } else printf("  WARN: could not open log file %s\n", logPath);
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 3; }
    s_rtp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_rtp == INVALID_SOCKET) { printf("socket failed\n"); return 3; }
    s_ssrc = (uint32_t)GetTickCount();
    signal(SIGINT, on_sigint);
    timeBeginPeriod(1);                       /* 1 ms scheduler tick */

    /* Disable console QuickEdit: a stray click/selection in the window otherwise
     * PAUSES the whole process (a multi-100 ms video freeze). Keep extended flags. */
    {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD cmode = 0;
        if (GetConsoleMode(hIn, &cmode))
            SetConsoleMode(hIn, (cmode & ~0x0040u) | 0x0080u); /* ~ENABLE_QUICK_EDIT | ENABLE_EXTENDED_FLAGS */
    }

    /* One-time, blocking setup of the two peripherals (fine to block here).
     * Generous timeout + retries + small gaps: the open/init sequence is several
     * back-to-back round-trips and the host occasionally drops a reply. */
    rcp_set_timeout_ms(600); rcp_set_retries(2);
    for (i = 0; i < 5 && s_spi == UINT16_MAX; ++i) { spi_setup(); if (s_spi == UINT16_MAX) Sleep(50); }
    Sleep(40);   /* gap before the I2C open so its reply isn't dropped after SPI's */
    for (i = 0; i < 5 && !s_proxInit; ++i) {
        if (s_i2c == UINT16_MAX) i2c_setup();
        if (s_i2c != UINT16_MAX) s_proxInit = vcnl4200_init();
        if (!s_proxInit) Sleep(50);
    }
    printf("  setup: thumbstick(SPI)=%s  proximity(I2C/VCNL4200)=%s\n",
           s_spi != UINT16_MAX ? "OK" : "FAILED", s_proxInit ? "OK" : "FAILED");
    elog("SETUP", 0, s_spi != UINT16_MAX, s_proxInit, 0, 0.0);

    /* Steady-cadence loop, DECOUPLED from the sensor round-trips. Every tick we
     * render both halves from the LATEST async sensor values and send one RTP
     * frame FIRST, then fire ONE sensor read (alternating thumbstick/proximity so
     * the two never go out back-to-back -> the host can't drop one of two replies).
     * Read replies land asynchronously on the rx thread and update the shared
     * state whenever they arrive. Because rtp_send() no longer sits behind any
     * blocking read, an occasional ~150 ms Windows scheduler stall in this loop
     * can no longer freeze the video - the next tick just carries on.
     * (The old scheme blocked on each read + a Sleep(12) between them; a stalled
     *  Sleep then froze the whole pipeline incl. the video -> the visible hitch.) */
    /* Async deadline. The T1S wire answers in ~3-4 ms, but the host's rx-dispatch
     * thread occasionally falls ~40-50 ms behind in bursts; with a too-short 40 ms
     * deadline a reply that IS already on the wire gets discarded as "timed out"
     * and re-read for nothing. 70 ms clears that rx jitter (so a late reply is still
     * delivered and the value updates) yet a genuinely lost reply still recovers in
     * ~70 ms, ~2x faster than the old 120 ms. (Verified by log.004: every timeout's
     * session id was present as a response on the wire -> 100% host-side, ~4 ms RTT.) */
    rcp_set_async_timeout_ms(70);
    {
        unsigned tick = 0;
        DWORD lastPrint = 0;
        while (s_run) {
            /* render from the latest values, then send the frame (steady cadence) */
            thumb_spot(s_tx, s_ty, bright);    /* display 1 (left): orange spot   */
            prox_bar(s_prox, proxMax, barBlue);/* display 2 (right): blue prox bar */
            rtp_send();                        /* ONE 20x10 frame -> both displays */
            elog("FRAME", (s_seq - 1u) & 0xFFFFu, s_tx, s_prox, 0, 0.0);  /* sid col = RTP seq */

            /* One read per tick, ALTERNATING thumbstick/proximity (1:1) so each
             * sensor is sampled at ~half the frame rate (~23 Hz). The old 3:1 bias
             * starved the proximity to ~12 Hz, which read as a stuttering bar; both
             * are now equal. Still exactly one request per tick, so replies arrive
             * solo (the host drops one of two that come back-to-back). If the
             * sensor due this tick still has a reply pending, fire the other so the
             * tick is not wasted. */
            if (tick & 1u) { if (!fire_prox())  fire_thumb(); }
            else           { if (!fire_thumb()) fire_prox();  }
            tick++;

            rcp_async_poll();   /* deliver replies / time-outs */

            /* throttle the console to ~10 Hz: WriteConsole can stall the loop for
             * tens of ms, which used to surface as a video hitch. */
            {
                DWORD now = GetTickCount();
                if (now - lastPrint >= 100u) {
                    printf("\r  Thumbstick x=%4u y=%4u %s | Proximity raw=%5u %s   ",
                           s_tx, s_ty, s_thumbOk ? "  " : "..", s_prox, s_proxOk ? "  " : "..");
                    fflush(stdout);
                    lastPrint = now;
                }
            }
            Sleep((DWORD)(1000 / fps));   /* frame cadence */
        }
    }

    /* clear both displays and release the peripherals */
    printf("\nStopping - clearing displays ...\n");
    memset(s_fb, 0, sizeof(s_fb));
    rtp_send(); Sleep(30); rtp_send();
    if (s_spi != UINT16_MAX) { CloseSpiVar_t c; c.HandleSpi = s_spi; rcp_close_spi(&c); }
    if (s_i2c != UINT16_MAX) { CloseI2CVar_t c; c.HandleI2C = s_i2c; rcp_close_i2c(&c); }
    if (s_log) { elog("STOP", 0, 0, 0, 0, 0.0); fclose(s_log); s_log = NULL; }
    timeEndPeriod(1);
    closesocket(s_rtp); WSACleanup();
    return 0;
}
